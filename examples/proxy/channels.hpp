#pragma once

#include <trellis/trellis.hpp>

using channel_numbers = trellis::channel_type_reliable_unordered<struct numbers_t>;

template <template <typename...> class T>
using apply_channels = T<channel_numbers>;
