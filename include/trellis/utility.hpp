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

// tuple_constructor

template <typename T>
struct tuple_constructor;

template <typename... Ts>
struct tuple_constructor<std::tuple<Ts...>> {
    template <typename Arg>
    auto operator()(Arg&& arg) const {
        return std::forward_as_tuple(std::forward<Arg>(arg));
    }
};

} // namespace trellis
