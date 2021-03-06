#pragma once

#include "channel_reliable.hpp"

namespace trellis::_detail {

/**
 * Channel implementing a reliable sequenced protocol.
 * NOTE: Currently susceptible to unbounded memory usage.
 */
class channel_reliable_sequenced : public channel_reliable {
public:
    channel_reliable_sequenced(connection_base& conn) : channel_reliable(conn) {}

    void send_packet(const headers::data& header, const shared_datagram_buffer& datagram, std::size_t size) {
        conn->get_context().dispatch([this, header, datagram, size, conn_ptr = conn->shared_from_this()]{
            // Forget all outgoing packets except for the latest in the sequence.
            outgoing_queue.remove_all_if([&](const outgoing_entry& e) {
                return sequence_id_less(e.header.sequence_id, header.sequence_id);
            }, conn_ptr);

            send_packet_impl(header, datagram, size);
        });
    }

    template <typename F>
    void receive(const headers::data& header, const datagram_buffer& datagram, size_t count, const F& on_receive_func) {
        // should only be called from the connections's receive handler, so we should be in the networking thread
        assert(conn->get_context().is_thread_current());

        if (auto result = receive_impl(header, datagram, count)) {
            auto iter = *result;

            auto& assembler = iter->second;

            assert(!assembler.is_cancelled());

            TRELLIS_LOG_ACTION("channel", +header.channel_id, "Calling on_receive_func for sequence_id ", incoming_sequence_id, ".");

            on_receive_func(raw_buffer{assembler.release(), assembler.size()});

            for (auto i = incoming_sequence_id; !sequence_id_less(header.sequence_id, i); ++i) {
                auto iter = assemblers.find(i);

                if (iter != assemblers.end()) {
                    assemblers.erase(iter);
                }
            }

            incoming_sequence_id = header.sequence_id + 1;
        }
    }
};

} // namespace trellis::_detail
