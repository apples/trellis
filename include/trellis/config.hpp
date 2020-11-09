#pragma once

#include <cstdint>

namespace trellis::config {

inline constexpr std::size_t datagram_size = 1200;
inline constexpr std::size_t max_fragments = 256;
inline constexpr std::size_t assembler_slots = 256;

} // namespace trellis::config
