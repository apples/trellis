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

namespace trellis {

class proxy_context {
public:
    using protocol = asio::ip::udp;

    proxy_context(asio::io_context& io) :
        io(&io),
        proxy_socket(io),
        sender_endpoint(),
        proxy_buffer(),
        connections(),
        running(false) {}

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

private:
    struct proxy_connection {
        protocol::endpoint client_endpoint;
        protocol::endpoint sender_endpoint;
        protocol::socket socket;
        datagram_buffer buffer;
    };

    void receive() {
        proxy_socket.async_receive_from(asio::buffer(proxy_buffer), sender_endpoint, [this](asio::error_code ec, std::size_t size) {
            if (ec.value() == asio::error::operation_aborted || !running) {
                std::cerr << "[trellis] PROXY shutting down" << std::endl;
                return;
            } else if (ec) {
                std::cerr << "[trellis] PROXY ERROR receive: " << ec.category().name() << ": " << ec.message() << std::endl;
                receive();
                return;
            }

            TRELLIS_LOG_DATAGRAM("prox", proxy_buffer, size);

            auto iter = connections.find(sender_endpoint);

            if (iter == connections.end()) {
                std::cout << "[trellis] PROXY new client " << sender_endpoint << std::endl;

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

            std::cout << "[trellis] PROXY client " << iter->second.client_endpoint << " == " << iter->second.socket.local_endpoint() << " => server " << remote_endpoint << std::endl;

            iter->second.socket.async_send_to(asio::buffer(proxy_buffer, size), remote_endpoint, [this, sz = size](asio::error_code ec, std::size_t size) {
                if (ec && ec.value() != asio::error::operation_aborted) {
                    std::cerr << "[trellis] PROXY ERROR while sending packet to " << remote_endpoint << ": " << ec.category().name() << ": " << ec.message() << std::endl;
                } else if (!ec) {
                    assert(size == sz);
                }
            });

            receive();
        });
    }

    void receive(proxy_connection& conn) {
        conn.socket.async_receive_from(asio::buffer(conn.buffer), conn.sender_endpoint, [this, &conn](asio::error_code ec, std::size_t size) {
            if (ec.value() == asio::error::operation_aborted || !running) {
                return;
            } else if (ec) {
                std::cerr << "[trellis] PROXY ERROR connection receive: " << ec.category().name() << ": " << ec.message() << std::endl;
                receive(conn);
                return;
            }

            TRELLIS_LOG_DATAGRAM("prox", conn.buffer, size);

            assert(conn.sender_endpoint == remote_endpoint);

            std::cout << "[trellis] PROXY server " << remote_endpoint << " == " << proxy_socket.local_endpoint() << " => client " << conn.client_endpoint << std::endl;

            proxy_socket.async_send_to(asio::buffer(conn.buffer, size), conn.client_endpoint, [this, &conn, sz = size](asio::error_code ec, std::size_t size) {
                if (ec && ec.value() != asio::error::operation_aborted) {
                    std::cerr << "[trellis] PROXY ERROR while sending packet to " << conn.client_endpoint << ": " << ec.category().name() << ": " << ec.message() << std::endl;
                } else if (!ec) {
                    assert(size == sz);
                }
            });

            receive(conn);
        });
    }

    asio::io_context* io;
    protocol::socket proxy_socket;
    protocol::endpoint remote_endpoint;
    protocol::endpoint sender_endpoint;
    datagram_buffer proxy_buffer;
    std::map<protocol::endpoint, proxy_connection> connections;
    bool running;
};

} // namespace trellis
