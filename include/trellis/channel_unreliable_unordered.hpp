#pragma once

#include "channel_unreliable.hpp"

namespace trellis::_detail {

/** Channel implementing an unreliable unordered protocol. */
class channel_unreliable_unordered : public channel_unreliable {
public:
    channel_unreliable_unordered(connection_base& conn) : channel_unreliable(conn) {}

    template <typename F>
    void receive(const headers::data& header, const datagram_buffer& datagram, size_t count, const F& on_receive_func) {
        // should only be called from the connections's receive handler, so we should be in the networking thread
        assert(conn->get_context().is_thread_current());

        if (auto data = receive_impl(header, datagram, count)) {
            on_receive_func(std::move(*data));
        }
    }
};

} // namespace trellis::_detail
