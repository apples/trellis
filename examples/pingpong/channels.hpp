#pragma once

#include <trellis/trellis.hpp>

using channel_pingpong = trellis::channel_type_unreliable_unordered<struct pingpong_t>;
using channel_pingpong_r = trellis::channel_type_reliable_unordered<struct pingpong_r_t>;

template <template <typename...> class T>
using apply_channels = T<channel_pingpong, channel_pingpong_r>;
