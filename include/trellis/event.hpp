#pragma once

#include "raw_buffer.hpp"

#include <memory>
#include <variant>

namespace trellis::_detail {

struct event_connect {
    using connection_ptr = std::shared_ptr<void>;

    connection_ptr conn;
};

struct event_disconnect {
    using connection_ptr = std::shared_ptr<void>;

    connection_ptr conn;
    asio::error_code ec;
};

class event_receive {
public:
    using connection_ptr = std::shared_ptr<void>;

    connection_ptr conn;
    std::uint8_t channel_id;
    raw_buffer data;
};

using event = std::variant<event_connect, event_disconnect, event_receive>;

} // namespace trellis::_detail
