#pragma once

#include <variant>

struct message_numbers {
    int number;

    template <typename Archive>
    void serialize(Archive& archive) {
        archive(number);
    }
};
