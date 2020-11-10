#pragma once

#include <trellis/trellis.hpp>

using channel_numbers = trellis::channel_def<struct numbers_t, false, false>;

template <template <typename...> class T>
using apply_channels = T<channel_numbers>;
