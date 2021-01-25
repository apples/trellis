#pragma once

#include "channel_reliable.hpp"

namespace trellis::_detail {

/**
 * Channel implementing a reliable unordered protocol.
 * NOTE: Currently susceptible to unbounded memory usage.
 */
class channel_reliable_unordered : public channel_reliable {
public:
    channel_reliable_unordered(connection_base& conn) : channel_reliable(conn) {}

    void send_packet(const headers::data& header, const shared_datagram_buffer& datagram, std::size_t size) {
        send_packet_impl(header, datagram, size);
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
        }
    }
};

} // namespace trellis::_detail
