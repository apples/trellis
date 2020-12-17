#pragma once

#include "inputs.hpp"
#include "tiny_math.hpp"

#include <trellis/trellis.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/optional.hpp>

#include <variant>

namespace message {

struct player_init {
    int id;
    tiny_vec<2> pos;

    template <typename Archive>
    void serialize(Archive& archive) {
        archive(id);
        archive(pos.x, pos.y);
    }
};

struct player_updates {
    struct pinfo {
        int id;
        tiny_vec<2> pos;

        template <typename Archive>
        void serialize(Archive& archive) {
            archive(id);
            archive(pos.x, pos.y);
        }
    };

    std::vector<pinfo> players;

    template <typename Archive>
    void serialize(Archive& archive) {
        archive(players);
    }
};

struct player_input {
    bool inputs[static_cast<int>(input_keycode::NUM_INPUTS)];

    template <typename Archive>
    void serialize(Archive& archive) {
        archive(inputs);
    }
};

using any = std::variant<
    std::monostate,
    player_init,
    player_updates,
    player_input>;

} // namespace message

template <typename Channel, typename Conn>
void send_message(const message::any& msg, Conn& conn) {
    auto ostream = trellis::opacketstream(conn);
    {
        auto archive = cereal::BinaryOutputArchive(ostream);
        archive(msg);
    }
    ostream.template send<Channel>();
}
