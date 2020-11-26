#pragma once

#include <string>

struct message_numbers {
    int number;
    std::string padding;

    template <typename Archive>
    void serialize(Archive& archive) {
        archive(number);
        archive(padding);
    }
};
