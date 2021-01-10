#pragma once

#include "utility.hpp"

#include <asio.hpp>

#include <tuple>
#include <type_traits>

namespace trellis {

template <typename Context>
struct context_traits;

template <template <typename...> typename Context, typename... Channels>
struct context_traits<Context<Channels...>> {
    using context_type = Context<Channels...>;
    using protocol = asio::ip::udp;

    template <template <typename, typename> class Channel, typename Connection>
    using channel_tuple = std::tuple<Channel<Connection, Channels>...>;

    template <typename Channel>
    static constexpr auto channel_index = index_of<Channel, Channels...>();

    static constexpr auto channel_count = sizeof...(Channels);

    template <int N, typename F>
    static constexpr void with_channel_type(int i, F&& func) {
        if constexpr (N < sizeof...(Channels)) {
            if (i == N) {
                func(nth_type_t<N, Channels...>{});
            } else {
                with_channel_type<N + 1>(i, std::forward<F>(func));
            }
        } else {
            // this should be unreachable
            assert(false);
            std::terminate();
        }
    }

    template <typename F>
    static constexpr void with_channel_type(int i, F&& func) {
        with_channel_type<0>(i, std::forward<F>(func));
    }
};

} // namespace trellis
