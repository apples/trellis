#pragma once

#include <asio.hpp>

#include <array>
#include <iostream>
#include <tuple>
#include <string_view>

namespace trellis {

template <typename T, bool R, bool O>
struct channel_traits {
    using tag_t = T;
    static constexpr bool is_reliable = false;
    static constexpr bool is_ordered = false;
};

template <typename Channel>
struct channel_state;

template <typename T>
struct channel_state<channel_traits<T, false, false>> {

};

template <typename... Channels>
class server_context {
public:
    using endpoint = asio::ip::udp::endpoint;

    server_context(asio::io_context& io, int port) : io(&io), port(port), socket(io, endpoint(asio::ip::udp::v4(), port)) {}

    void receive() {
        socket.async_receive_from(asio::buffer(buffer), sender_endpoint, [this](asio::error_code ec, std::size_t size) {
            auto str = std::string_view(buffer.data(), size);

            std::cout << "Recieved \"" << str << "\" from " << sender_endpoint << std::endl;

            if (str == "ping") {
                send(sender_endpoint, "pong");
            }

            receive();
        });
    }

    void send(const endpoint& client_address, const std::string& message) {
        socket.async_send_to(asio::buffer(message), client_address, [=](asio::error_code ec, std::size_t size) {
            std::cout << "Sent \"" << message << "\" to " << client_address << std::endl;
        });
    }

private:
    asio::io_context* io;
    int port;
    asio::ip::udp::socket socket;
    endpoint sender_endpoint;
    std::array<char, 64*1024> buffer;
    std::tuple<channel_state<Channels>...> channels;
};

template <typename... Channels>
class client_context {
public:
    using endpoint = asio::ip::udp::endpoint;

    client_context(asio::io_context& io, const endpoint& client_endpoint, const endpoint& server_endpoint) :
        io(&io),
        client_endpoint(client_endpoint),
        server_endpoint(server_endpoint),
        socket(io, client_endpoint) {}

    void receive() {
        socket.async_receive_from(asio::buffer(buffer), sender_endpoint, [this](asio::error_code ec, std::size_t size) {
            auto str = std::string_view(buffer.data(), size);

            std::cout << "Recieved \"" << str << "\" from " << sender_endpoint << std::endl;

            receive();
        });
    }

    void send(const std::string& message) {
        socket.async_send_to(asio::buffer(message), server_endpoint, [=](asio::error_code ec, std::size_t size) {
            std::cout << "Sent \"" << message << "\" to " << server_endpoint << std::endl;
        });
    }

private:
    asio::io_context* io;
    endpoint server_endpoint;
    asio::ip::udp::socket socket;
    endpoint client_endpoint;
    endpoint sender_endpoint;
    std::array<char, 64*1024> buffer;
    std::tuple<channel_state<Channels>...> channels;
};

} // namespace trellis
