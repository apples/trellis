#pragma once

#include "connection.hpp"
#include "datagram.hpp"
#include "message_header.hpp"

#include <asio.hpp>

#include <cstring>
#include <cassert>
#include <iostream>
#include <memory>
#include <map>
#include <random>

namespace trellis {

class proxy_context {
public:
    using protocol = asio::ip::udp;

    struct proxy_stats {
        int client_messages = 0;
        int client_messages_dropped = 0;
        int server_messages = 0;
        int server_messages_dropped = 0;
    };

    proxy_context(asio::io_context& io) :
        io(&io),
        proxy_socket(io),
        sender_endpoint(),
        proxy_buffer(),
        connections(),
        running(false),
        rng(std::random_device{}()),
        client_drop_rate(0),
        server_drop_rate(0),
        stats{} {}

    void listen(const protocol::endpoint& proxy_endpoint, const protocol::endpoint& remote_endpoint) {
        proxy_socket.open(proxy_endpoint.protocol());
        proxy_socket.bind(proxy_endpoint);
        this->remote_endpoint = remote_endpoint;
        running = true;
        receive();
    }

    auto get_endpoint() const -> typename protocol::endpoint {
        return proxy_socket.local_endpoint();
    }

    void disconnect_all() {
        connections.clear();
    }

    void stop() {
        running = false;
        disconnect_all();
        proxy_socket.shutdown(asio::socket_base::shutdown_both);
        proxy_socket.close();
    }

    void set_client_drop_rate(double chance) {
        client_drop_rate = chance;
    }

    void set_server_drop_rate(double chance) {
        server_drop_rate = chance;
    }

    auto get_stats() const -> const proxy_stats& {
        return stats;
    }

private:
    struct proxy_connection {
        protocol::endpoint client_endpoint;
        protocol::endpoint sender_endpoint;
        protocol::socket socket;
        _detail::datagram_storage buffer;
    };

    // Client => Server
    void receive() {
        proxy_socket.async_receive_from(asio::buffer(proxy_buffer.data), sender_endpoint, [this](asio::error_code ec, std::size_t size) {
            TRELLIS_BEGIN_SECTION("proxy");

            if (ec.value() == asio::error::operation_aborted || !running) {
                return;
            } else if (ec) {
                std::cerr << "[trellis] PROXY ERROR receive: " << ec.category().name() << ": " << ec.message() << std::endl;
                receive();
                return;
            }

            ++stats.client_messages;

            TRELLIS_LOG_DATAGRAM("prox", proxy_buffer.data, size);

            auto iter = connections.find(sender_endpoint);

            if (iter == connections.end()) {
                TRELLIS_LOG_ACTION("proxy", sender_endpoint, "New client");

                auto conn = proxy_connection{
                    sender_endpoint,
                    {},
                    protocol::socket(*io),
                    {},
                };

                auto endpoint = protocol::endpoint(protocol::v4(), 0);

                conn.socket.open(endpoint.protocol());
                conn.socket.bind(endpoint);

                auto [new_iter, success] = connections.emplace(sender_endpoint, std::move(conn));

                assert(success);

                iter = new_iter;

                receive(iter->second);
            }

            assert(iter != connections.end());
            assert(iter->second.client_endpoint == sender_endpoint);

            auto drop_roll = std::uniform_real_distribution<>{0.0, 1.0}(rng);

            if (drop_roll < client_drop_rate) {
                TRELLIS_LOG_ACTION("proxy", iter->second.client_endpoint, "Dropped packet");
                ++stats.client_messages_dropped;
            } else {
                TRELLIS_LOG_ACTION("proxy", iter->second.client_endpoint, "Sending client ", iter->second.client_endpoint, " == ", iter->second.socket.local_endpoint(), " => server ", remote_endpoint);

                auto buffer = cache.make_pending_buffer();

                std::memcpy(buffer.data(), proxy_buffer.data.data(), size);

                iter->second.socket.async_send_to(buffer.buffer(size), remote_endpoint, [this, buffer, sz = size](asio::error_code ec, std::size_t size) {
                    if (ec && ec.value() != asio::error::operation_aborted) {
                        std::cerr << "[trellis] PROXY ERROR while sending packet to " << remote_endpoint << ": " << ec.category().name() << ": " << ec.message() << std::endl;
                    } else if (!ec) {
                        assert(size == sz);
                    }
                });
            }

            TRELLIS_END_SECTION("proxy");

            receive();
        });
    }

    // Server => Client
    void receive(proxy_connection& conn) {
        conn.socket.async_receive_from(asio::buffer(conn.buffer), conn.sender_endpoint, [this, &conn](asio::error_code ec, std::size_t size) {
            if (ec.value() == asio::error::operation_aborted || !running) {
                return;
            } else if (ec) {
                std::cerr << "[trellis] PROXY ERROR connection receive: " << ec.category().name() << ": " << ec.message() << std::endl;
                receive(conn);
                return;
            }

            ++stats.server_messages;

            TRELLIS_LOG_DATAGRAM("prox", conn.buffer, size);

            assert(conn.sender_endpoint == remote_endpoint);

            auto drop_roll = std::uniform_real_distribution<>{0.0, 1.0}(rng);

            if (drop_roll < server_drop_rate) {
                TRELLIS_LOG_ACTION("proxy", -1, "Dropped packet");
                ++stats.server_messages_dropped;
            } else {
                TRELLIS_LOG_ACTION("proxy", -1, "Sending server ", remote_endpoint, " == ", proxy_socket.local_endpoint(), " => client ", conn.client_endpoint);

                auto buffer = cache.make_pending_buffer();

                std::memcpy(buffer.data(), conn.buffer.data(), size);

                proxy_socket.async_send_to(buffer.buffer(size), conn.client_endpoint, [this, &conn, buffer, sz = size](asio::error_code ec, std::size_t size) {
                    if (ec && ec.value() != asio::error::operation_aborted) {
                        std::cerr << "[trellis] PROXY ERROR while sending packet to " << conn.client_endpoint << ": " << ec.category().name() << ": " << ec.message() << std::endl;
                    } else if (!ec) {
                        assert(size == sz);
                    }
                });
            }

            receive(conn);
        });
    }

    asio::io_context* io;
    protocol::socket proxy_socket;
    protocol::endpoint remote_endpoint;
    protocol::endpoint sender_endpoint;
    _detail::datagram_buffer proxy_buffer;
    _detail::datagram_buffer_cache cache;
    std::map<protocol::endpoint, proxy_connection> connections;
    bool running;
    std::mt19937 rng;
    double client_drop_rate;
    double server_drop_rate;
    proxy_stats stats;
};

} // namespace trellis
