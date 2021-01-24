#pragma once

#include "channels.hpp"
#include "message.hpp"
#include "overload.hpp"
#include "tiny_engine.hpp"

#include <trellis/trellis.hpp>
#include <trellis/proxy_context.hpp>

#include <asio.hpp>

#include <cstddef>
#include <memory>
#include <unordered_map>

using client_context = channel::apply_channels<trellis::client_context>;
using connection_type = client_context::connection_type;
using connection_ptr = std::shared_ptr<connection_type>;

class scene_gameplay final : public tiny_scene_base {
public:
    scene_gameplay(tiny_engine& e, client_context* ctx, const std::string& server_ip, int server_port) :
        engine(&e),
        client(ctx),
        renderer(e, 426, 240),
        sprites_texture("assets/sprites.png"),
        inputs{},
        inputs_changed(false),
        my_player{},
        other_players{},
        bullets{} {
            std::cout << "Client connecting to [" << server_ip << "]:" << server_port << std::endl;

            auto addr = asio::ip::make_address(server_ip);
            client->connect({addr.is_v4() ? asio::ip::udp::v4() : asio::ip::udp::v6(), 0}, {addr, static_cast<unsigned short>(server_port)});

            renderer.set_camera_size({426, 240});
            renderer.set_camera_pos({0, 0});
        }

    virtual void handle_event(tiny_engine& engine, const SDL_Event& event) override {
        switch (event.type) {
            case SDL_EventType::SDL_KEYDOWN:
                if (event.key.repeat == 0) {
                    switch (event.key.keysym.scancode) {
                        case SDL_Scancode::SDL_SCANCODE_LEFT:
                            inputs_changed = true;
                            inputs[static_cast<int>(input_keycode::LEFT)] = true;
                            break;
                        case SDL_Scancode::SDL_SCANCODE_RIGHT:
                            inputs_changed = true;
                            inputs[static_cast<int>(input_keycode::RIGHT)] = true;
                            break;
                        case SDL_Scancode::SDL_SCANCODE_UP:
                            inputs_changed = true;
                            inputs[static_cast<int>(input_keycode::UP)] = true;
                            break;
                        case SDL_Scancode::SDL_SCANCODE_DOWN:
                            inputs_changed = true;
                            inputs[static_cast<int>(input_keycode::DOWN)] = true;
                            break;
                        case SDL_Scancode::SDL_SCANCODE_SPACE:
                            if (my_player) {
                                if (auto conn = wconn.lock()) {
                                    send_message<channel::reliable_messages>(message::player_shoot{
                                        my_player->id,
                                        my_player->pos,
                                        my_player->dir,
                                    }, *conn);
                                }
                            }
                            break;
                        default:
                            break;
                    }
                }
                break;
            case SDL_EventType::SDL_KEYUP:
                switch (event.key.keysym.scancode) {
                    case SDL_Scancode::SDL_SCANCODE_LEFT:
                        inputs_changed = true;
                        inputs[static_cast<int>(input_keycode::LEFT)] = false;
                        break;
                    case SDL_Scancode::SDL_SCANCODE_RIGHT:
                        inputs_changed = true;
                        inputs[static_cast<int>(input_keycode::RIGHT)] = false;
                        break;
                    case SDL_Scancode::SDL_SCANCODE_UP:
                        inputs_changed = true;
                        inputs[static_cast<int>(input_keycode::UP)] = false;
                        break;
                    case SDL_Scancode::SDL_SCANCODE_DOWN:
                        inputs_changed = true;
                        inputs[static_cast<int>(input_keycode::DOWN)] = false;
                        break;
                    default:
                        break;
                }
                break;
        }
    }

    virtual void update(tiny_engine& engine) override {
        client->poll_events(*this);

        if (inputs_changed) {
            if (auto conn = wconn.lock(); conn && my_player) {
                auto msg = message::player_input{};
                std::copy_n(inputs, sizeof(inputs), msg.inputs);
                send_message<channel::state_updates>(msg, *conn);
                std::cout << "Input update (" << my_player->id << "): [" <<
                    (inputs[0] ? "O" : "-") <<
                    (inputs[1] ? "O" : "-") <<
                    (inputs[2] ? "O" : "-") <<
                    (inputs[3] ? "O" : "-") <<
                    "]" << std::endl;
                inputs_changed = false;
            }
        }
    }

    virtual void draw(tiny_engine& engine) override {
        renderer.begin();

        if (my_player) {
            //renderer.set_camera_pos(my_player->pos);
            renderer.draw_sprite(my_player->pos, {16, 16}, my_player->dir / 32.f * tiny_tau<>, sprites_texture, {0,0}, {16, 16});
        }

        for (auto& [id, p] : other_players) {
            renderer.draw_sprite(p.pos, {16,16}, p.dir / 32.f * tiny_tau<>, sprites_texture, {16,0}, {16, 16});
        }

        for (auto& [id, b] : bullets) {
            if (my_player && b.player_id == my_player->id) {
                renderer.draw_sprite(b.pos, {8,8}, b.dir / 32.f * tiny_tau<>, sprites_texture, {32,0}, {8, 8});
            } else {
                renderer.draw_sprite(b.pos, {8,8}, b.dir / 32.f * tiny_tau<>, sprites_texture, {40,0}, {8, 8});
            }
        }

        renderer.finish();
    }

    void stop() {
        client->stop();
        engine->stop();
    }

    void on_connect(const connection_ptr& conn) {
        std::cout << "Connected!" << std::endl;
        wconn = conn;
    }

    void on_disconnect(const connection_ptr& conn, asio::error_code ec) {
        if (ec) {
            std::cout << "Connection error: " << ec.message() << std::endl;
        } else {
            std::cout << "Disconnected." << std::endl;
        }
        stop();
    }

    void on_receive(channel::sync, const connection_ptr& conn, std::istream& istream) {
        on_receive(conn, istream);
    }

    void on_receive(channel::state_updates, const connection_ptr& conn, std::istream& istream) {
        on_receive(conn, istream);
    }

    void on_receive(channel::reliable_messages, const connection_ptr& conn, std::istream& istream) {
        on_receive(conn, istream);
    }

private:
    void on_receive(const connection_ptr& conn, std::istream& istream) {
        message::any msg;
        {
            auto archive = cereal::BinaryInputArchive(istream);
            archive(msg);
        }
        std::visit(overload {
            [](std::monostate) {},
            [&](const auto&) {
                std::cout << "Bad server!" << std::endl;
                conn->disconnect();
            },
            [&](const message::player_init& m) {
                std::cout << "Player ID: " << m.id << std::endl;

                my_player = player_info{
                    m.id,
                    m.pos,
                    m.dir,
                };
                other_players.erase(m.id);
            },
            [&](const message::player_updates& m) {
                for (const auto& p : m.players) {
                    if (my_player && p.id == my_player->id) {
                        my_player->pos = p.pos;
                        my_player->dir = p.dir;
                    } else {
                        auto iter = other_players.find(p.id);
                        if (iter == other_players.end()) {
                            other_players.insert_or_assign(p.id, player_info{
                                p.id,
                                p.pos,
                                p.dir,
                            });
                        } else {
                            iter->second.pos = p.pos;
                            iter->second.dir = p.dir;
                        }
                    }
                }
                for (const auto& b : m.bullets) {
                    auto iter = bullets.find(b.id);
                    if (iter == bullets.end()) {
                        bullets.insert_or_assign(b.id, bullet_info{
                            b.id,
                            b.player_id,
                            b.pos,
                            b.dir,
                        });
                    } else {
                        iter->second.pos = b.pos;
                        iter->second.dir = b.dir;
                    }
                }
            },
            [&](const message::remove_player& rm) {
                if (my_player && my_player->id == rm.id) {
                    std::cout << "Removing self AAAAAAAAAAAH!!!" << std::endl;
                    my_player = std::nullopt;
                } else {
                    auto iter = other_players.find(rm.id);
                    if (iter != other_players.end()) {
                        std::cout << "Removing player " << rm.id << std::endl;
                        other_players.erase(iter);
                    } else {
                        std::cout << "Failed to remove ghost " << rm.id << std::endl;
                    }
                }
            },
            [&](const message::remove_bullet& rm) {
                auto iter = bullets.find(rm.id);
                if (iter != bullets.end()) {
                    bullets.erase(iter);
                }
            },
        }, msg);
    }

    tiny_engine* engine;
    client_context* client;
    connection_ptr::weak_type wconn;
    tiny_renderer renderer;
    tiny_texture sprites_texture;

    struct player_info {
        int id;
        tiny_vec<2> pos;
        int dir;
    };

    struct bullet_info {
        int id;
        int player_id;
        tiny_vec<2> pos;
        int dir;
    };

    bool inputs[4];
    bool inputs_changed;
    std::optional<player_info> my_player;
    std::unordered_map<int, player_info> other_players;
    std::unordered_map<int, bullet_info> bullets;
};

void run_client(asio::io_context& io, const std::string& server_ip, int server_port) {
    SDL_SetMainReady();
    SDL_Init(SDL_INIT_EVERYTHING);

    auto engine = tiny_engine(io, "Asteroids", 854, 480);

    auto ctx = client_context(engine.get_io());

    engine.queue_scene<scene_gameplay>(&ctx, server_ip, server_port);

    engine.main_loop();

    SDL_Quit();
}
