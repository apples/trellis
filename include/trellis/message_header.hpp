#pragma once

#include <cstdint>
#include <variant>

namespace trellis::headers {

enum type : std::uint8_t {
    CONNECT,
    CONNECT_OK,
    CONNECT_ACK,
    DISCONNECT,
    DATA,
};

struct connect {
};

struct connect_ok {
    std::uint16_t connection_id;
};

struct connect_ack {
    std::uint16_t connection_id;
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
