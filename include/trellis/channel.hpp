#pragma once

#include "channel_types.hpp"
#include "config.hpp"
#include "datagram.hpp"
#include "fragment_assembler.hpp"
#include "message_header.hpp"
#include "logging.hpp"
#include "retry_queue.hpp"
#include "streams.hpp"

#include <limits>
#include <unordered_map>

namespace trellis {

inline constexpr auto sequence_id_less(config::sequence_id_t a, config::sequence_id_t b) -> bool {
    using limits = std::numeric_limits<config::sequence_id_t>;

    constexpr auto max_gap = limits::max() / 2 - limits::min() / 2;

    return (a != b) && (b - a <= max_gap);
}

// Unreliable Unordered

/** Channel implementing an unreliable unordered protocol. */
class channel_unreliable_unordered {
public:
    channel_unreliable_unordered(connection_base& conn) :
        conn(&conn),
        sequence_id(0),
        assemblers() {}

    channel_unreliable_unordered(const channel_unreliable_unordered&) = delete;
    channel_unreliable_unordered(channel_unreliable_unordered&&) = delete;

    auto next_sequence_id() -> config::sequence_id_t {
        return sequence_id++;
    }

    void send_packet([[maybe_unused]] const headers::data& header, const shared_datagram_buffer& datagram, std::size_t size) {
        conn->send_raw(datagram, size);
    }

    template <typename F>
    void receive(const headers::data& header, const datagram_buffer& datagram, size_t count, const F& on_receive_func) {
        assert(count <= config::datagram_size);
        assert(count >= headers::data_offset);

        if (header.fragment_count == 1) {
            // Shortcut for non-fragmented packets

            TRELLIS_LOG_ACTION("channel", +header.channel_id, "Processing message ", header.sequence_id, " as non-fragmented.");

            assert(header.fragment_id == 0);
            auto b = datagram.data.data() + headers::data_offset;
            auto e = datagram.data.data() + count;
            assert(count <= config::datagram_size);
            auto istream = ibytestream(b, e);
            on_receive_func(istream);
        } else {
            TRELLIS_LOG_ACTION("channel", +header.channel_id, "Processing message ", header.sequence_id, " as fragment piece ", +header.fragment_id, " / ", +header.fragment_count, ".");

            assert(header.fragment_id < header.fragment_count);
            auto slot = header.sequence_id % config::assembler_slots;
            auto& assembler = assemblers[slot];

            auto is_stale = [&]() -> bool {
                auto& current_sid = assembler.get_sequence_id();
                return !current_sid || sequence_id_less(*current_sid, header.sequence_id);
            }();

            if (is_stale) {
                TRELLIS_LOG_ACTION("channel", +header.channel_id, "Resetting assembler in slot ", slot, ".");

                assembler.reset(header.sequence_id, header.fragment_count);
            }

            if (assembler.get_sequence_id() == header.sequence_id) {
                TRELLIS_LOG_ACTION("channel", +header.channel_id, "Handing packet to assembler in slot ", slot, ".");

                auto b = datagram.data.data() + headers::data_offset;
                auto e = datagram.data.data() + count;
                assert(count <= config::datagram_size);
                assembler.receive(header, b, e);

                if (assembler.is_complete()) {
                    TRELLIS_LOG_ACTION("channel", +header.channel_id, "Message reassembly is complete, calling on_receive_func.");

                    auto istream = ibytestream(assembler.data(), assembler.data() + assembler.size());
                    on_receive_func(istream);
                }
            }
        }
    }

    void receive_ack(const headers::data_ack& header) {
        TRELLIS_LOG_ACTION("channel", +header.channel_id, "Received unexpected DATA_ACK (sid:", header.sequence_id, ",fid:", header.fragment_id, "). Disconnecting.");
        conn->disconnect();
    }

private:
    connection_base* conn;
    config::sequence_id_t sequence_id;
    std::array<fragment_assembler, config::assembler_slots> assemblers;
};

// Reliable

/**
 * Channel implementing a reliable ordered protocol.
 * NOTE: Currently susceptible to unbounded memory usage.
 */
class channel_reliable {
public:
    using timer_type = connection_base::timer_type;

    channel_reliable(connection_base& conn) :
        conn(&conn),
        sequence_id(0),
        incoming_sequence_id(0),
        last_expected_sequence_id(0),
        assemblers(),
        outgoing_queue(conn.get_context().get_io(), [this](const outgoing_entry& e){ send_outgoing(e); }) {}

    channel_reliable(const channel_reliable&) = delete;
    channel_reliable(channel_reliable&&) = delete;

    auto next_sequence_id() -> config::sequence_id_t {
        return sequence_id++;
    }

    void send_packet(const headers::data& header, const shared_datagram_buffer& datagram, std::size_t size) {
        conn->send_raw(datagram, size);

        outgoing_queue.push({
            header,
            datagram,
            size,
        });
    }

    template <typename F>
    void receive_impl(const headers::data& header, const datagram_buffer& datagram, size_t count, const F& on_complete_func) {
        assert(count >= headers::data_offset);
        assert(count <= config::datagram_size);
        assert(header.fragment_id < header.fragment_count);

        TRELLIS_LOG_ACTION("channel", +header.channel_id, "Processing message ", header.sequence_id, " as fragment piece ", +header.fragment_id, " / ", +header.fragment_count, ".");

        if (sequence_id_less(header.sequence_id, incoming_sequence_id)) {
            TRELLIS_LOG_ACTION("channel", +header.channel_id, "Message ", header.sequence_id, ", fragment piece ", +header.fragment_id, " received duplicate. Expected: ", incoming_sequence_id, ".");
            conn->send_ack(header.channel_id, header.sequence_id, incoming_sequence_id, header.fragment_id);
            return;
        }

        auto iter = assemblers.find(header.sequence_id);

        if (iter == assemblers.end()) {
            auto [new_iter, success] = assemblers.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(header.sequence_id),
                std::forward_as_tuple(header.sequence_id, header.fragment_count));

            assert(success);

            iter = new_iter;
        }

        auto& assembler = iter->second;

        assert(assembler.get_sequence_id() == header.sequence_id);

        if (assembler.has_fragment(header.fragment_id)) {
            TRELLIS_LOG_ACTION("channel", +header.channel_id, "Assembler for sequence_id ", header.sequence_id, " already has fragment ", +header.fragment_id, ". Ignoring.");

            // If the assembler for the next incoming packet is complete, it should have been processed and removed already.
            assert(header.sequence_id != incoming_sequence_id || !assembler.is_complete());
        } else {
            TRELLIS_LOG_ACTION("channel", +header.channel_id, "Handing packet to assembler for sequence_id ", header.sequence_id, ".");

            auto b = datagram.data.data() + headers::data_offset;
            auto e = datagram.data.data() + count;
            assembler.receive(header, b, e);

            if (assembler.is_complete()) {
                TRELLIS_LOG_ACTION("channel", +header.channel_id, "Message reassembly is complete, calling on_complete_func.");

                on_complete_func(iter);
            }
        }

        conn->send_ack(header.channel_id, header.sequence_id, incoming_sequence_id, header.fragment_id);
    }

    void receive_ack(const headers::data_ack& header) {
        TRELLIS_LOG_ACTION("channel", +header.channel_id, "Received DATA_ACK (sid:", header.sequence_id, ",fid:", +header.fragment_id, ").");

        [[maybe_unused]] auto success = false;

        if (sequence_id_less(last_expected_sequence_id, header.expected_sequence_id)) {
            success = outgoing_queue.remove_all_if([&](const outgoing_entry& e) {
                return
                    sequence_id_less(e.header.sequence_id, header.expected_sequence_id) ||
                    (e.header.sequence_id == header.sequence_id && e.header.fragment_id == header.fragment_id);
            });

            last_expected_sequence_id = header.expected_sequence_id;
        } else {
            success = outgoing_queue.remove_one_if([&](const outgoing_entry& e) {
                return e.header.sequence_id == header.sequence_id && e.header.fragment_id == header.fragment_id;
            });
        }

        if (success) {
            TRELLIS_LOG_ACTION("channel", +header.channel_id, "DATA_ACK corresponded to outgoing packet.");
        } else {
            TRELLIS_LOG_ACTION("channel", +header.channel_id, "DATA_ACK did not correspond to any outgoing packet.");
        }
    }

protected:
    struct outgoing_entry {
        headers::data header;
        shared_datagram_buffer datagram;
        std::size_t size;
    };

    void send_outgoing(const outgoing_entry& entry) {
        TRELLIS_LOG_ACTION("channel", +entry.header.channel_id, "Resending outgoing packet (", entry.header.sequence_id, ").");
        conn->send_raw(entry.datagram, entry.size);
    }

    connection_base* conn;
    config::sequence_id_t sequence_id;
    config::sequence_id_t incoming_sequence_id;
    config::sequence_id_t last_expected_sequence_id;
    std::unordered_map<config::sequence_id_t, fragment_assembler> assemblers;
    retry_queue<outgoing_entry, timer_type> outgoing_queue;
};

// Reliable Ordered

/**
 * Channel implementing a reliable ordered protocol.
 * NOTE: Currently susceptible to unbounded memory usage.
 */
class channel_reliable_ordered : public channel_reliable {
public:
    channel_reliable_ordered(connection_base& conn) : channel_reliable(conn) {}

    template <typename F>
    void receive(const headers::data& header, const datagram_buffer& datagram, size_t count, const F& on_receive_func) {
        receive_impl(header, datagram, count, [&](auto iter) {
            if (header.sequence_id == incoming_sequence_id) {
                TRELLIS_LOG_ACTION("channel", +header.channel_id, "Message reassembly is complete, posting sequence.");

                while (iter != assemblers.end() && iter->second.is_complete()) {
                    auto& assembler = iter->second;

                    assert(assembler.get_sequence_id() == incoming_sequence_id);

                    auto istream = ibytestream(assembler.data(), assembler.data() + assembler.size());

                    TRELLIS_LOG_ACTION("channel", +header.channel_id, "Calling on_receive_func for sequence_id ", incoming_sequence_id, ".");
                    on_receive_func(istream);

                    assemblers.erase(iter);

                    ++incoming_sequence_id;

                    iter = assemblers.find(incoming_sequence_id);
                }
            }
        });
    }
};

// Reliable Unordered

/**
 * Channel implementing a reliable unordered protocol.
 * NOTE: Currently susceptible to unbounded memory usage.
 */
class channel_reliable_unordered : public channel_reliable {
public:
    channel_reliable_unordered(connection_base& conn) : channel_reliable(conn) {}

    template <typename F>
    void receive(const headers::data& header, const datagram_buffer& datagram, size_t count, const F& on_receive_func) {
        receive_impl(header, datagram, count, [&](auto iter) {
            auto& assembler = iter->second;

            assert(!assembler.is_cancelled());

            auto istream = ibytestream(assembler.data(), assembler.data() + assembler.size());

            TRELLIS_LOG_ACTION("channel", +header.channel_id, "Calling on_receive_func for sequence_id ", incoming_sequence_id, ".");
            on_receive_func(istream);

            assembler.cancel();

            if (header.sequence_id == incoming_sequence_id) {
                TRELLIS_LOG_ACTION("channel", +header.channel_id, "Message for incoming_sequence_id completed, clearing sequence.");

                while (iter != assemblers.end() && iter->second.is_complete()) {
                    auto& assembler = iter->second;

                    assert(assembler.get_sequence_id() == incoming_sequence_id);
                    assert(assembler.is_cancelled());

                    assemblers.erase(iter);

                    ++incoming_sequence_id;

                    iter = assemblers.find(incoming_sequence_id);
                }
            }
        });
    }
};

// Reliable Sequenced

/**
 * Channel implementing a reliable sequenced protocol.
 * NOTE: Currently susceptible to unbounded memory usage.
 */
class channel_reliable_sequenced : public channel_reliable {
public:
    channel_reliable_sequenced(connection_base& conn) : channel_reliable(conn) {}

    template <typename F>
    void receive(const headers::data& header, const datagram_buffer& datagram, size_t count, const F& on_receive_func) {
        receive_impl(header, datagram, count, [&](auto iter) {
            auto& assembler = iter->second;

            assert(!assembler.is_cancelled());

            auto istream = ibytestream(assembler.data(), assembler.data() + assembler.size());

            TRELLIS_LOG_ACTION("channel", +header.channel_id, "Calling on_receive_func for sequence_id ", incoming_sequence_id, ".");
            on_receive_func(istream);

            for (auto i = incoming_sequence_id; !sequence_id_less(header.sequence_id, i); ++i) {
                auto iter = assemblers.find(i);

                if (iter != assemblers.end()) {
                    assemblers.erase(iter);
                }
            }

            incoming_sequence_id = header.sequence_id + 1;
        });
    }
};

// Templated

/** Connection-specific channel type. */
template <typename Connection, typename ChannelDef>
class channel;

template <typename Connection, typename Tag>
class channel<Connection, channel_type_unreliable_unordered<Tag>> : public channel_unreliable_unordered {
public:
    using channel_unreliable_unordered::channel_unreliable_unordered;
};

template <typename Connection, typename Tag>
class channel<Connection, channel_type_reliable_ordered<Tag>> : public channel_reliable_ordered {
public:
    using channel_reliable_ordered::channel_reliable_ordered;
};

template <typename Connection, typename Tag>
class channel<Connection, channel_type_reliable_unordered<Tag>> : public channel_reliable_unordered {
public:
    using channel_reliable_unordered::channel_reliable_unordered;
};

template <typename Connection, typename Tag>
class channel<Connection, channel_type_reliable_sequenced<Tag>> : public channel_reliable_sequenced {
public:
    using channel_reliable_sequenced::channel_reliable_sequenced;
};


} // namespace trellis
