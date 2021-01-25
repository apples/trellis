#pragma once

#include <cstdint>
#include <limits>

namespace trellis::config {

inline constexpr std::size_t datagram_size = 1200;
inline constexpr std::size_t max_fragments = 256;
inline constexpr std::size_t assembler_slots = 256;

using sequence_id_t = std::uint32_t;
using fragment_id_t = std::uint8_t;

} // namespace trellis::config

namespace trellis::_detail {

inline constexpr auto sequence_id_less(config::sequence_id_t a, config::sequence_id_t b) -> bool {
    using limits = std::numeric_limits<config::sequence_id_t>;

    constexpr auto max_gap = limits::max() / 2 - limits::min() / 2;

    return (a != b) && (b - a <= max_gap);
}

} // namespace trellis::_detail
