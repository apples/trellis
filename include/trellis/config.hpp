#pragma once

#include <cstdint>

namespace trellis::config {

inline constexpr std::size_t datagram_size = 1200;
inline constexpr std::size_t max_fragments = 256;
inline constexpr std::size_t assembler_slots = 256;

using sequence_id_t = std::uint32_t;
using fragment_id_t = std::uint8_t;

} // namespace trellis::config
