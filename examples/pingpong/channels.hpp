#pragma once

#include <trellis/trellis.hpp>

using channel_pingpong = trellis::channel_def<struct pingpong_t, false, false>;

template <template <typename...> class T>
using apply_channels = T<channel_pingpong>;
