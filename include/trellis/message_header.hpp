#pragma once

#include <cstdint>
#include <variant>

namespace trellis::headers {

enum type : std::uint8_t {
    CONNECT,
    CONNECT_OK,
    DISCONNECT,
    DATA,
};

struct connect {
};

struct connect_ok {
};

struct disconnect {
};

struct data {
    std::uint16_t sequence_id;
    std::uint8_t channel_id;
    std::uint8_t fragment_count;
    std::uint8_t fragment_id;
};

constexpr std::size_t data_offset = sizeof(type) + sizeof(data);

} // namespace trellis::headers
