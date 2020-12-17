#pragma once

#include <trellis/trellis.hpp>

using channel_pingpong = trellis::channel_def<struct pingpong_t, false, false>;
using channel_pingpong_r = trellis::channel_def<struct pingpong_t, true, false>;

template <template <typename...> class T>
using apply_channels = T<channel_pingpong, channel_pingpong_r>;
