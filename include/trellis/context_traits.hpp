#pragma once

#include <asio.hpp>

#include <tuple>
#include <type_traits>

namespace trellis {

template <typename C, typename H, typename... T>
struct index_of : std::integral_constant<std::size_t, 1 + index_of<C, T...>::value> {};

template <typename C, typename... T>
struct index_of<C, C, T...> : std::integral_constant<std::size_t, 0> {};

template <typename C, typename... Ts>
constexpr auto index_of_v = index_of<C, Ts...>::value;

template <typename Context>
struct context_traits;

template <template <typename...> typename Context, typename... Channels>
struct context_traits<Context<Channels...>> {
    using context_type = Context<Channels...>;
    using protocol = asio::ip::udp;

    template <typename Channel>
    static constexpr auto channel_index = index_of_v<Channel, Channels...>;

    static constexpr auto channel_count = sizeof...(Channels);
};

} // namespace trellis
