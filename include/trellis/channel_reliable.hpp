#pragma once

#include "config.hpp"
#include "datagram.hpp"
#include "fragment_assembler.hpp"
#include "message_header.hpp"
#include "logging.hpp"
#include "retry_queue.hpp"
#include "streams.hpp"
#include "connection_stats.hpp"

#include <unordered_map>

namespace trellis {

/**
 * Channel implementing a reliable ordered protocol.
 * NOTE: Currently susceptible to unbounded memory usage.
 */
class channel_reliable {
public:
    using assembler_map = std::unordered_map<config::sequence_id_t, fragment_assembler>;

    channel_reliable(connection_base& conn) :
        conn(&conn),
        sequence_id(0),
        incoming_sequence_id(0),
        last_expected_sequence_id(0),
        assemblers(),
        outgoing_queue(conn.get_context().get_io(), conn.get_context().get_executor(), [this](const outgoing_entry& e){ send_outgoing(e); }) {}

    channel_reliable(const channel_reliable&) = delete;
    channel_reliable(channel_reliable&&) = delete;

    auto next_sequence_id() -> config::sequence_id_t {
        return sequence_id++;
    }

    void receive_ack(const headers::data_ack& header) {
        // should only be called from the connections's receive handler, so we should be in the networking thread
        assert(conn->get_context().is_thread_current());

        TRELLIS_LOG_ACTION("channel", +header.channel_id, "Received DATA_ACK (sid:", header.sequence_id, ",fid:", +header.fragment_id, "eid:", header.expected_sequence_id, ").");

        [[maybe_unused]] auto success = false;

        if (sequence_id_less(last_expected_sequence_id, header.expected_sequence_id)) {
            success = outgoing_queue.remove_all_if([&](const outgoing_entry& e) {
                return
                    sequence_id_less(e.header.sequence_id, header.expected_sequence_id) ||
                    (e.header.sequence_id == header.sequence_id && e.header.fragment_id == header.fragment_id);
            }, conn->weak_from_this());

            last_expected_sequence_id = header.expected_sequence_id;
        } else {
            success = outgoing_queue.remove_one_if([&](const outgoing_entry& e) {
                return e.header.sequence_id == header.sequence_id && e.header.fragment_id == header.fragment_id;
            }, conn->weak_from_this());
        }

        if (success) {
            TRELLIS_LOG_ACTION("channel", +header.channel_id, "DATA_ACK corresponded to outgoing packet.");
        } else {
            TRELLIS_LOG_ACTION("channel", +header.channel_id, "DATA_ACK did not correspond to any outgoing packet.");
        }
    }

    auto get_stats() const -> connection_stats {
        return {
            int(outgoing_queue.size()),
            int(assemblers.size()),
        };
    }

protected:
    struct outgoing_entry {
        headers::data header;
        shared_datagram_buffer datagram;
        std::size_t size;
    };

    void send_packet_impl(const headers::data& header, const shared_datagram_buffer& datagram, std::size_t size) {
        conn->get_context().dispatch([this, header, datagram, size, conn_ptr = conn->shared_from_this()]{
            conn->send_raw(datagram, size);

            outgoing_queue.push({
                header,
                datagram,
                size,
            }, conn_ptr);
        });
    }

    void send_outgoing(const outgoing_entry& entry) {
        // should only be called from the outgoing_queue's timer, so we should be in the networking thread
        assert(conn->get_context().is_thread_current());

        TRELLIS_LOG_ACTION("channel", +entry.header.channel_id, "Resending outgoing packet (", entry.header.sequence_id, ").");
        conn->send_raw(entry.datagram, entry.size);
    }

    auto receive_impl(const headers::data& header, const datagram_buffer& datagram, size_t count) -> std::optional<assembler_map::iterator> {
        // should only be called from the connections's receive handler, so we should be in the networking thread
        assert(conn->get_context().is_thread_current());

        assert(count >= headers::data_offset);
        assert(count <= config::datagram_size);
        assert(header.fragment_id < header.fragment_count);

        TRELLIS_LOG_ACTION("channel", +header.channel_id, "Processing message ", header.sequence_id, " as fragment piece ", +header.fragment_id, " / ", +header.fragment_count, ".");

        if (sequence_id_less(header.sequence_id, incoming_sequence_id)) {
            TRELLIS_LOG_ACTION("channel", +header.channel_id, "Message ", header.sequence_id, ", fragment piece ", +header.fragment_id, " received duplicate. Expected: ", incoming_sequence_id, ".");
            conn->send_ack(header.channel_id, header.sequence_id, incoming_sequence_id, header.fragment_id);
            return std::nullopt;
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

        auto result = std::optional<assembler_map::iterator>{};

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

                result = iter;
            }
        }

        conn->send_ack(header.channel_id, header.sequence_id, incoming_sequence_id, header.fragment_id);

        return result;
    }

    connection_base* conn;
    std::atomic<config::sequence_id_t> sequence_id;
    config::sequence_id_t incoming_sequence_id;
    config::sequence_id_t last_expected_sequence_id;
    assembler_map assemblers;
    retry_queue<outgoing_entry, connection_base, context_base::executor_type> outgoing_queue;
};

} // namespace trellis
