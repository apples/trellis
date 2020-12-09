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

template <typename Connection, typename ChannelDef>
class channel;

template <typename Connection, typename Tag>
class channel<Connection, channel_type_unreliable_unordered<Tag>> {
public:
    using connection_type = Connection;

    channel(connection_type& conn) :
        conn(&conn),
        sequence_id(0),
        assemblers() {}

    channel(const channel&) = delete;
    channel(channel&&) = delete;

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
    connection_type* conn;
    config::sequence_id_t sequence_id;
    std::array<fragment_assembler, config::assembler_slots> assemblers;
};

template <typename Connection, typename Tag>
class channel<Connection, channel_type_reliable_ordered<Tag>> {
public:
    using connection_type = Connection;
    using timer_type = typename connection_type::timer_type;

    channel(connection_type& conn) :
        conn(&conn),
        sequence_id(0),
        incoming_sequence_id(0),
        assemblers(),
        outgoing_queue(conn.get_context().get_io(), [this](const outgoing_entry& e){ send_outgoing(e); }) {}

    channel(const channel&) = delete;
    channel(channel&&) = delete;

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
    void receive(const headers::data& header, const datagram_buffer& datagram, size_t count, const F& on_receive_func) {
        assert(count >= headers::data_offset);
        assert(count <= config::datagram_size);
        assert(header.fragment_id < header.fragment_count);

        TRELLIS_LOG_ACTION("channel", +header.channel_id, "Processing message ", header.sequence_id, " as fragment piece ", +header.fragment_id, " / ", +header.fragment_count, ".");

        conn->send_ack(header.channel_id, header.sequence_id, header.fragment_id);

        if (sequence_id_less(header.sequence_id, incoming_sequence_id)) {
            TRELLIS_LOG_ACTION("channel", +header.channel_id, "Message ", header.sequence_id, ", fragment piece ", +header.fragment_id, " received duplicate. Expected: ", incoming_sequence_id, ".");
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

            if (header.sequence_id == incoming_sequence_id && assembler.is_complete()) {
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
        }
    }

    void receive_ack(const headers::data_ack& header) {
        TRELLIS_LOG_ACTION("channel", +header.channel_id, "Received DATA_ACK (sid:", header.sequence_id, ",fid:", +header.fragment_id, ").");

        auto success = outgoing_queue.remove_if([&](const outgoing_entry& e) {
            return e.header.sequence_id == header.sequence_id && e.header.fragment_id == header.fragment_id;
        });

        if (success) {
            TRELLIS_LOG_ACTION("channel", +header.channel_id, "DATA_ACK corresponded to outgoing packet.");
        } else {
            TRELLIS_LOG_ACTION("channel", +header.channel_id, "DATA_ACK did not correspond to any outgoing packet.");
        }
    }

private:
    struct outgoing_entry {
        headers::data header;
        shared_datagram_buffer datagram;
        std::size_t size;
    };

    void send_outgoing(const outgoing_entry& entry) {
        TRELLIS_LOG_ACTION("channel", +entry.header.channel_id, "Resending outgoing packet (", entry.header.sequence_id, ").");
        conn->send_raw(entry.datagram, entry.size);
    }

    connection_type* conn;
    config::sequence_id_t sequence_id;
    config::sequence_id_t incoming_sequence_id;
    std::unordered_map<config::sequence_id_t, fragment_assembler> assemblers;
    retry_queue<outgoing_entry, timer_type> outgoing_queue;
};

} // namespace trellis
