#pragma once

#include "channels.hpp"
#include "inputs.hpp"
#include "message.hpp"
#include "overload.hpp"
#include "tiny_math.hpp"

#include <cereal/archives/binary.hpp>
#include <trellis/trellis.hpp>

#include <asio.hpp>

#include <cstddef>
#include <chrono>
#include <memory>

class server_engine {
public:
    using server_context = channel::apply_channels<trellis::server_context>;
    using connection_type = server_context::connection_type;
    using connection_ptr = std::shared_ptr<connection_type>;
    using endpoint_type = server_context::protocol::endpoint;
    using timer_type = asio::steady_timer;
    using clock = timer_type::clock_type;

    static constexpr float max_player_vel = 700.f;
    static constexpr float player_accel = 2000.f;
    static constexpr float bullet_vel = 320.f;
    static constexpr float max_bullet_age = 0.7f;

    struct physics_body {
        tiny_vec<2> pos = {0, 0};
        tiny_vec<2> vel = {0, 0};
        int dir = 0; // Range: 0-31
    };

    struct player_data {
        int id = 0;
        bool inputs[static_cast<int>(input_keycode::NUM_INPUTS)] = {};
        connection_ptr::weak_type conn = {};
        physics_body* body = nullptr;
    };

    struct bullet_data {
        int id = 0;
        int player_id = 0;
        float age = 0;
        physics_body* body = nullptr;
    };

    server_engine(asio::io_context& io, int port) :
        io(&io),
        server(io),
        tick_rate(clock::duration{std::chrono::seconds{1}} / 60),
        timer(io),
        log_timer(io),
        players(),
        next_player_id(0),
        bullets(),
        next_bullet_id(0),
        physics_bodies() {
            server.listen({asio::ip::udp::v6(), static_cast<unsigned short>(port)});
            std::cout << "Server listening." << std::endl;
        }

    void run() {
        std::cout << "Server running." << std::endl;
        io->post([this]{ tick(1.f / 60.f); });
        io->post([this]{ log_stats(); });
        io->run();
    }

    void stop() {
        std::cout << "Server stopped." << std::endl;
        timer.cancel();
        log_timer.cancel();
        server.stop();
    }

    void on_connect(const connection_ptr& conn) {
        auto iter = players.find(conn->get_endpoint());
        if (iter != players.end()) {
            std::cout << "Player " << iter->second.id << " attempted to connect twice." << std::endl;
            conn->disconnect();
        } else {
            physics_bodies.push_back(std::make_unique<physics_body>(physics_body{
                {0, 0},
                {0, 0},
                0,
            }));

            auto new_player = player_data{
                next_player_id++,
                {},
                conn,
                physics_bodies.back().get(),
            };

            std::cout << "New player: " << new_player.id << std::endl;

            players.emplace(conn->get_endpoint(), new_player);

            auto msg = message::player_init{
                new_player.id,
                new_player.body->pos,
                new_player.body->dir,
            };

            send_message<channel::sync>(msg, *conn);
        }
    }

    void on_disconnect(const connection_ptr& conn, asio::error_code ec) {
        auto iter = players.find(conn->get_endpoint());
        if (iter != players.end()) {
            std::cout << "Player " << iter->second.id << " disconnected: ";
            auto msg = message::remove_player{iter->second.id};
            players.erase(iter);
            for (auto& [endpoint, player] : players) {
                if (auto pconn = player.conn.lock()) {
                    send_message<channel::reliable_messages>(msg, *pconn);
                }
            }
        } else {
            std::cout << "Ghost disconnected: ";
        }
        if (ec) {
            std::cout << "Error: " << ec.message() << std::endl;
        } else {
            std::cout << "Disconnected." << std::endl;
        }
    }

    void on_receive(channel::sync, const connection_ptr& conn, std::istream& istream) {
        message::any msg;
        {
            auto archive = cereal::BinaryInputArchive(istream);
            archive(msg);
        }

        std::visit(overload {
            [&](const auto&) {
                conn->disconnect();
            },
        }, msg);
    }

    void on_receive(channel::state_updates, const connection_ptr& conn, std::istream& istream) {
        message::any msg;
        {
            auto archive = cereal::BinaryInputArchive(istream);
            archive(msg);
        }

        auto iter = players.find(conn->get_endpoint());
        if (iter == players.end()) {
            conn->disconnect();
        } else {
            std::visit(overload {
                [&](const auto&) {
                    conn->disconnect();
                },
                [&](const message::player_input& m) {
                    std::copy_n(m.inputs, sizeof(m.inputs), iter->second.inputs);
                    std::cout << "Input update (" << iter->second.id << "): [" <<
                        (iter->second.inputs[0] ? "O" : "-") <<
                        (iter->second.inputs[1] ? "O" : "-") <<
                        (iter->second.inputs[2] ? "O" : "-") <<
                        (iter->second.inputs[3] ? "O" : "-") <<
                        "]" << std::endl;
                },
            }, msg);
        }
    }

    void on_receive(channel::reliable_messages, const connection_ptr& conn, std::istream& istream) {
        message::any msg;
        {
            auto archive = cereal::BinaryInputArchive(istream);
            archive(msg);
        }

        auto iter = players.find(conn->get_endpoint());
        if (iter == players.end()) {
            conn->disconnect();
        } else {
            std::visit(overload {
                [&](const auto&) {
                    conn->disconnect();
                },
                [&](const message::player_shoot& s) {
                    physics_bodies.push_back(std::make_unique<physics_body>(physics_body{
                        s.pos,
                        rotate(tiny_vec<2>{bullet_vel, 0}, float(s.dir) / 32.f * tiny_tau<>),
                        s.dir,
                    }));

                    auto new_bullet = bullet_data{
                        next_bullet_id++,
                        s.player_id,
                        0,
                        physics_bodies.back().get(),
                    };

                    bullets.emplace_back(new_bullet);
                },
            }, msg);
        }
    }

private:
    void tick(float delta) {
        auto start = clock::now();

        // Networking
        server.poll_events(*this);

        // Bullets
        {
            for (auto& bullet : bullets) {
                bullet.age += delta;
            }

            auto split = std::partition(bullets.begin(), bullets.end(), [](auto& b){ return b.age < max_bullet_age; });
            
            for (auto& [endpoint, player] : players) {
                if (auto conn = player.conn.lock()) {
                    for (auto iter = split; iter != bullets.end(); ++iter) {
                        send_message<channel::reliable_messages>(message::remove_bullet{iter->id}, *conn);
                    }
                }
            }

            bullets.erase(split, bullets.end());
        }

        // Player input
        for (auto& [endpoint, player] : players) {
            int x_axis = 0;
            int y_axis = 0;

            if (player.inputs[static_cast<int>(input_keycode::LEFT)]) {
                x_axis -= 1;
            }
            if (player.inputs[static_cast<int>(input_keycode::RIGHT)]) {
                x_axis += 1;
            }
            if (player.inputs[static_cast<int>(input_keycode::DOWN)]) {
                y_axis -= 1;
            }
            if (player.inputs[static_cast<int>(input_keycode::UP)]) {
                y_axis += 1;
            }

            int face_towards = -1;

            if (y_axis < 0) {
                if (x_axis < 0) {
                    face_towards = 20;
                } else if (x_axis > 0) {
                    face_towards = 28;
                } else {
                    face_towards = 24;
                }
            } else if (y_axis > 0) {
                if (x_axis < 0) {
                    face_towards = 12;
                } else if (x_axis > 0) {
                    face_towards = 4;
                } else {
                    face_towards = 8;
                }
            } else {
                if (x_axis < 0) {
                    face_towards = 16;
                } else if (x_axis > 0) {
                    face_towards = 0;
                }
            }

            if (face_towards != -1) {
                int dist = face_towards - player.body->dir;

                if (dist != 0) {
                    // turn towards desired direction

                    if (std::abs(dist) > 16) {
                        if (dist > 0) {
                            dist = -32 + dist;
                        } else {
                            dist = 32 + dist;
                        }
                    }

                    dist = dist / std::abs(dist);

                    player.body->dir = (player.body->dir + dist) % 32;

                    if (player.body->dir < 0 ) {
                        player.body->dir += 32;
                    }
                } else {
                    // already facing desired direction, accelerate

                    auto desired_vel = rotate(tiny_vec<2>{max_player_vel, 0}, float(player.body->dir) / 32.f * tiny_tau<float>);

                    auto accel_needed = desired_vel - player.body->vel;

                    if (length(accel_needed) > 0) {
                        auto accel = normalize(accel_needed) * player_accel;

                        player.body->vel = player.body->vel + accel * delta;
                    }
                }
            }
        }

        // Physics
        {
            for (auto& body : physics_bodies) {
                body->pos = body->pos + body->vel * delta;

                if (body->pos.x < -222) {
                    body->pos.x += 444;
                }
                if (body->pos.x > 222) {
                    body->pos.x -= 444;
                }
                if (body->pos.y < -128) {
                    body->pos.y += 256;
                }
                if (body->pos.y > 128) {
                    body->pos.y -= 256;
                }
            }
        }

        // Player update messages
        {
            auto msg = message::player_updates{};

            for (auto iter = players.begin(); iter != players.end();) {
                auto& [endpoint, player] = *iter;
                if (auto conn = player.conn.lock()) {
                    msg.players.push_back({
                        player.id,
                        player.body->pos,
                        player.body->dir,
                    });
                    ++iter;
                } else {
                    iter = players.erase(iter);
                }
            }

            for (auto& bullet : bullets) {
                msg.bullets.push_back({
                    bullet.id,
                    bullet.player_id,
                    bullet.body->pos,
                    bullet.body->dir,
                });
            }

            for (auto& [endpoint, player] : players) {
                if (auto conn = player.conn.lock()) {
                    send_message<channel::state_updates>(msg, *conn);
                }
            }
        }

        timer.expires_from_now(tick_rate);
        timer.async_wait([this](asio::error_code ec) {
            if (ec) return;
            tick(1.f / 60.f);
        });

        auto stop = clock::now();
        frame_times.push_back(stop - start);
    }

    void log_stats() {
        auto total_frame_time = std::chrono::duration<double, std::milli>{std::accumulate(frame_times.begin(), frame_times.end(), clock::duration{0})};
        auto avg_frame_time = total_frame_time / frame_times.size();
        frame_times.clear();

        std::cout << "=== LOGGING STATS ===\n";
        std::cout << "Avg. frame time: " << avg_frame_time.count() << "ms\n";
        for (auto& [endpoint, player] : players) {
            if (auto conn = player.conn.lock()) {
                std::cout << conn->get_endpoint() << "\n";
                const auto& channel_stats = conn->get_stats();
                for (auto i = 0u; i < channel_stats.size(); ++i) {
                    auto [queue_size, num_waiting] = channel_stats[i];
                    std::cout << "  channel " << i << "\n";
                    std::cout << "    queue_size:  " << queue_size << "\n";
                    std::cout << "    num_waiting: " << num_waiting << "\n";
                }
            }
        }
        std::cout << std::flush;

        log_timer.expires_from_now(std::chrono::seconds{1});
        log_timer.async_wait([this](asio::error_code ec) {
            if (ec) return;
            log_stats();
        });
    }

    asio::io_service* io;
    server_context server;
    clock::duration tick_rate;
    timer_type timer;
    timer_type log_timer;
    std::map<endpoint_type, player_data> players;
    int next_player_id;
    std::vector<clock::duration> frame_times;
    std::vector<bullet_data> bullets;
    int next_bullet_id;
    std::vector<std::unique_ptr<physics_body>> physics_bodies;
};

void run_server(asio::io_context& io, int port) {
    auto server = server_engine(io, port);

    server.run();
}
