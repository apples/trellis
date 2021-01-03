#pragma once

#include "channel_unreliable.hpp"

namespace trellis {

/** Channel implementing an unreliable sequenced protocol. */
class channel_unreliable_sequenced : public channel_unreliable {
public:
    channel_unreliable_sequenced(connection_base& conn) :
        channel_unreliable(conn),
        incoming_sequence_id(0) {}

    template <typename F>
    void receive(const headers::data& header, const datagram_buffer& datagram, size_t count, const F& on_receive_func) {
        if (!sequence_id_less(header.sequence_id, incoming_sequence_id)) {
            if (auto data = receive_impl(header, datagram, count)) {
                auto istream = ibytestream(data->begin(), data->end());
                on_receive_func(istream);

                assert(!sequence_id_less(header.sequence_id, incoming_sequence_id));

                incoming_sequence_id = header.sequence_id + 1;
            }
        }
    }

private:
    config::sequence_id_t incoming_sequence_id;
};

} // namespace trellis
