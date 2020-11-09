#pragma once

#include <cstdint>
#include <type_traits>

namespace trellis {

template <typename C, typename H, typename... T>
struct index_of : std::integral_constant<std::size_t, 1 + index_of<C, T...>::value> {};

template <typename C, typename... T>
struct index_of<C, C, T...> : std::integral_constant<std::size_t, 0> {};

template <typename C, typename... Ts>
constexpr auto index_of_v = index_of<C, Ts...>::value;

} // namespace trellis
