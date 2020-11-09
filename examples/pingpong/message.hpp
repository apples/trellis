#pragma once

#include <variant>

struct message_ping {
    template <typename Archive>
    void serialize(Archive& archive) {}
};

struct message_pong {
    template <typename Archive>
    void serialize(Archive& archive) {}
};

using any_message = std::variant<message_ping, message_pong>;
