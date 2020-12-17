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

    struct player_data {
        int id = 0;
        tiny_vec<2> pos = {0, 0};
        bool inputs[static_cast<int>(input_keycode::NUM_INPUTS)] = {};
        connection_ptr::weak_type conn = {};
    };

    server_engine(asio::io_context& io, int port) :
        io(&io),
        server(io),
        tick_rate(clock::duration{std::chrono::seconds{1}} / 60),
        timer(io),
        players(),
        next_player_id(0) {
            server.on_connect([this](server_context&, const connection_ptr& conn) {
                this->on_connect(conn);
            });
            server.on_disconnect([this](server_context&, const connection_ptr& conn, asio::error_code ec) {
                this->on_disconnect(conn, ec);
            });
            server.on_receive<channel::sync>([this](server_context&, const connection_ptr& conn, std::istream& istream) {
                this->on_receive_sync(conn, istream);
            });
            server.on_receive<channel::state_updates>([this](server_context&, const connection_ptr& conn, std::istream& istream) {
                this->on_receive_state_updates(conn, istream);
            });
            server.listen({asio::ip::udp::v6(), static_cast<unsigned short>(port)});
            std::cout << "Server listening." << std::endl;
        }

    void run() {
        std::cout << "Server running." << std::endl;
        io->post([this]{ tick(); });
        io->run();
    }

    void stop() {
        std::cout << "Server stopped." << std::endl;
        timer.cancel();
        server.stop();
    }

private:
    void tick() {
        auto msg = message::player_updates{};

        for (auto iter = players.begin(); iter != players.end();) {
            auto& [endpoint, player] = *iter;
            if (auto conn = player.conn.lock()) {
                msg.players.push_back({
                    player.id,
                    player.pos,
                });
                ++iter;
            } else {
                iter = players.erase(iter);
            }
        }

        for (auto& [endpoint, player] : players) {
            if (auto conn = player.conn.lock()) {
                send_message<channel::state_updates>(msg, *conn);
            }
        }

        timer.expires_from_now(tick_rate);
        timer.async_wait([this](asio::error_code ec) {
            if (ec) return;
            tick();
        });
    }

    void on_connect(const connection_ptr& conn) {
        auto iter = players.find(conn->get_endpoint());
        if (iter != players.end()) {
            std::cout << "Player " << iter->second.id << " attempted to connect twice." << std::endl;
            conn->disconnect();
        } else {
            auto new_player = player_data{
                next_player_id++,
                {0, 0},
                {},
                conn,
            };

            std::cout << "New player: " << new_player.id << std::endl;

            players.emplace(conn->get_endpoint(), new_player);

            auto msg = message::player_init{
                new_player.id,
                new_player.pos,
            };

            send_message<channel::sync>(msg, *conn);
        }
    }

    void on_disconnect(const connection_ptr& conn, asio::error_code ec) {
        auto iter = players.find(conn->get_endpoint());
        if (iter != players.end()) {
            std::cout << "Player " << iter->second.id << " disconnected: ";
        } else {
            std::cout << "Ghost disconnected: ";
        }
        if (ec) {
            std::cout << "Error: " << ec.message() << std::endl;
        } else {
            std::cout << "Disconnected." << std::endl;
        }
    }

    void on_receive_sync(const connection_ptr& conn, std::istream& istream) {
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

    void on_receive_state_updates(const connection_ptr& conn, std::istream& istream) {
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

    asio::io_service* io;
    server_context server;
    clock::duration tick_rate;
    timer_type timer;
    std::map<endpoint_type, player_data> players;
    int next_player_id;
};

void run_server(asio::io_context& io, int port) {
    auto server = server_engine(io, port);

    server.run();
}