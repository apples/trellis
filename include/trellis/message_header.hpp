#pragma once

#include "config.hpp"

#include <cstdint>
#include <variant>

namespace trellis::headers {

enum type : std::uint8_t {
    CONNECT,
    CONNECT_OK,
    CONNECT_ACK,
    DISCONNECT,
    DATA,
    DATA_ACK,
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
    config::sequence_id_t sequence_id;
    std::uint8_t channel_id;
    config::fragment_id_t fragment_count;
    config::fragment_id_t fragment_id;
};

struct data_ack {
    config::sequence_id_t sequence_id;
    config::sequence_id_t expected_sequence_id;
    std::uint8_t channel_id;
    config::fragment_id_t fragment_id;
};

constexpr std::size_t data_offset = sizeof(type) + sizeof(data);

} // namespace trellis::headers
