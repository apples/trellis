#pragma once

#include "channel_reliable.hpp"

namespace trellis {

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

} // namespace trellis
