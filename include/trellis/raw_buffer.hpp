#pragma once

#include <cstdint>
#include <memory>

namespace trellis {

struct raw_buffer {
    std::unique_ptr<char[]> data;
    std::size_t data_len;
};

} // namespace trellis
