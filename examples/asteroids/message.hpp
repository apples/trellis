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
    int dir;

    template <typename Archive>
    void serialize(Archive& archive) {
        archive(id);
        archive(pos.x, pos.y);
        archive(dir);
    }
};

struct player_updates {
    struct pinfo {
        int id;
        tiny_vec<2> pos;
        int dir;

        template <typename Archive>
        void serialize(Archive& archive) {
            archive(id);
            archive(pos.x, pos.y);
            archive(dir);
        }
    };

    struct binfo {
        int id;
        int player_id;
        tiny_vec<2> pos;
        int dir;

        template <typename Archive>
        void serialize(Archive& archive) {
            archive(id);
            archive(player_id);
            archive(pos.x, pos.y);
            archive(dir);
        }
    };

    std::vector<pinfo> players;
    std::vector<binfo> bullets;

    template <typename Archive>
    void serialize(Archive& archive) {
        archive(players);
        archive(bullets);
    }
};

struct player_input {
    bool inputs[static_cast<int>(input_keycode::NUM_INPUTS)];

    template <typename Archive>
    void serialize(Archive& archive) {
        archive(inputs);
    }
};

struct remove_player {
    int id;

    template <typename Archive>
    void serialize(Archive& archive) {
        archive(id);
    }
};

struct remove_bullet {
    int id;

    template <typename Archive>
    void serialize(Archive& archive) {
        archive(id);
    }
};

struct player_shoot {
    int player_id;
    tiny_vec<2> pos;
    int dir;

    template <typename Archive>
    void serialize(Archive& archive) {
        archive(player_id);
        archive(pos.x, pos.y);
        archive(dir);
    }
};

using any = std::variant<
    std::monostate,
    player_init,
    player_updates,
    player_input,
    remove_player,
    remove_bullet,
    player_shoot>;

} // namespace message

template <typename Channel, typename Conn>
void send_message(const message::any& msg, Conn& conn) {
    //std::cout << "Sending message (type:" << msg.index() << ") to " << conn.get_endpoint() << "." << std::endl;
    auto ostream = trellis::opacketstream(conn);
    {
        auto archive = cereal::BinaryOutputArchive(ostream);
        archive(msg);
    }
    ostream.template send<Channel>();
}
