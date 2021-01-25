#pragma once

#include "utility.hpp"

#include <asio.hpp>

#include <tuple>
#include <type_traits>

namespace trellis {

template <typename Context>
struct context_traits;

/** Type traits for a context. */
template <template <typename...> typename Context, typename... Channels>
struct context_traits<Context<Channels...>> {
    using context_type = Context<Channels...>;
    using protocol = asio::ip::udp;

    /** Maps the channel list to a tuple of channel objects. */
    template <template <typename, typename> class Channel, typename Connection>
    using channel_tuple = std::tuple<Channel<Connection, Channels>...>;

    /** Gets the index of the given channel. */
    template <typename Channel>
    static constexpr auto channel_index = _detail::index_of<Channel, Channels...>();

    /** Gets the total number of channels. */
    static constexpr auto channel_count = sizeof...(Channels);

    /** Calls func with the channel type in the index i. */
    template <int N, typename F>
    static constexpr void with_channel_type(int i, F&& func) {
        if constexpr (N < sizeof...(Channels)) {
            if (i == N) {
                func(_detail::nth_type_t<N, Channels...>{});
            } else {
                with_channel_type<N + 1>(i, std::forward<F>(func));
            }
        } else {
            // this should be unreachable
            assert(false);
            std::terminate();
        }
    }

    /** Calls func with the channel type in the index i. */
    template <typename F>
    static constexpr void with_channel_type(int i, F&& func) {
        with_channel_type<0>(i, std::forward<F>(func));
    }
};

} // namespace trellis
