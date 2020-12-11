#pragma once

#include "channel_unreliable.hpp"

namespace trellis {

/** Channel implementing an unreliable unordered protocol. */
class channel_unreliable_unordered : public channel_unreliable {
public:
    channel_unreliable_unordered(connection_base& conn) : channel_unreliable(conn) {}

    template <typename F>
    void receive(const headers::data& header, const datagram_buffer& datagram, size_t count, const F& on_receive_func) {
        receive_impl(header, datagram, count, []{ return false; }, on_receive_func);
    }
};

} // namespace trellis
