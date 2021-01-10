#pragma once

#include <cstdint>
#include <tuple>
#include <type_traits>

namespace trellis {

// index_of

template <typename C, typename... Ts>
constexpr auto _index_of_impl() -> std::size_t {
    auto r = std::size_t(0);
    (void)((std::is_same_v<C, Ts> ? true : (r += 1, false)) || ...);
    return r;
}

template <typename C, typename... Ts>
constexpr auto index_of() -> std::size_t {
    static_assert(sizeof...(Ts) != 0, "List is empty");
    constexpr auto r = _index_of_impl<C, Ts...>();
    static_assert(r != sizeof...(Ts), "Type not found in list");
    return r;
}

// nth_type

template <int N, typename H, typename... Ts>
struct nth_type {
    using type = typename nth_type<N - 1, Ts...>::type;
};

template <typename H, typename... Ts>
struct nth_type<0, H, Ts...> {
    using type = H;
};

template <int N, typename... Ts>
using nth_type_t = typename nth_type<N, Ts...>::type;

// comma_t

template <typename A, typename B>
using comma_t = B;

// tuple_constructor

template <typename T>
struct tuple_constructor;

template <typename... Ts>
struct tuple_constructor<std::tuple<Ts...>> {
    template <typename Arg>
    auto operator()(Arg&& arg) const {
        return std::forward_as_tuple(std::forward<comma_t<Ts, Arg>>(arg)...);
    }
};

// overload

template <typename... Ts>
struct overload : Ts... {
    using Ts::operator()...;
};

template <typename... Ts>
overload(Ts...) -> overload<Ts...>;

} // namespace trellis
