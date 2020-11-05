#pragma once

#include <asio.hpp>

#include <cstring>
#include <cassert>
#include <array>
#include <iostream>
#include <tuple>
#include <memory>
#include <map>

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

enum message_type : std::uint8_t {
    CONNECT,
    CONNECT_OK,
    HEARTBEAT,
    DISCONNECT,
    DATA,
    ACK,
};

template <typename C>
struct context_traits;

template <template <typename...> typename C, typename... Channels>
struct context_traits<C<Channels...>> {
    using channel_state_tuple = std::tuple<channel_state<Channels>...>;
};

template <typename C>
class connection : public std::enable_shared_from_this<connection<C>> {
public:
    friend C;

    using context_type = C;
    using channel_state_tuple = typename context_traits<context_type>::channel_state_tuple;
    using buffer_iterator = typename context_type::buffer_iterator;

    using std::enable_shared_from_this<connection>::shared_from_this;

    connection(context_type& context, asio::ip::udp::socket& socket, asio::ip::udp::endpoint client_endpoint) :
        context(&context),
        socket(&socket),
        client_endpoint(client_endpoint),
        connected(true),
        channels() {}

    auto get_endpoint() const -> const asio::ip::udp::endpoint& {
        return client_endpoint;
    }

    void send_connect() {
        auto buffer = context->make_pending_buffer();
        auto type = message_type::CONNECT;
        buffer->resize(sizeof(message_type));
        std::memcpy(buffer->data(), &type, sizeof(message_type));
        send_raw(buffer);
    }

    void send_connect_ok() {
        auto buffer = context->make_pending_buffer();
        auto type = message_type::CONNECT_OK;
        buffer->resize(sizeof(message_type));
        std::memcpy(buffer->data(), &type, sizeof(message_type));
        send_raw(buffer);
    }

    void send_data(const std::string& data) {
        auto buffer = context->make_pending_buffer();
        auto type = message_type::DATA;
        buffer->resize(sizeof(message_type) + data.size());
        std::memcpy(buffer->data(), &type, sizeof(message_type));
        std::memcpy(buffer->data() + sizeof(message_type), data.data(), data.size());
        send_raw(buffer);
    }

    void disconnect() {
        if (!connected) return;

        connected = false;

        auto buffer = context->make_pending_buffer();
        auto type = message_type::DISCONNECT;
        buffer->resize(sizeof(message_type));
        std::memcpy(buffer->data(), &type, sizeof(message_type));

        socket->async_send_to(asio::buffer(*buffer), client_endpoint, [buffer, ptr = this->shared_from_this()](asio::error_code ec, std::size_t size) {
            ptr->context->free_pending_buffer(buffer);
            ptr->context->kill(*ptr);
        });
    }

private:
    void send_raw(buffer_iterator data) {
        if (!connected) return;

        socket->async_send_to(asio::buffer(*data), client_endpoint, [data, ptr = shared_from_this()](asio::error_code ec, std::size_t size) {
            ptr->context->free_pending_buffer(data);

            if (ec) {
                std::cerr << "Error: " << ec.category().name() << ": " << ec.message() << std::endl;
                ptr->disconnect();
            }
        });
    }

    context_type* context;
    asio::ip::udp::socket* socket;
    asio::ip::udp::endpoint client_endpoint;
    bool connected;
    channel_state_tuple channels;
};

template <typename C>
class base_context {
public:
    friend class connection<C>;

    using connection_type = connection<C>;
    using buffer_list = std::list<std::string>;
    using buffer_iterator = buffer_list::iterator;

    base_context() :
        pending_buffers(),
        next_buffer(pending_buffers.end()) {}

private:
    auto make_pending_buffer() -> buffer_iterator {
        if (next_buffer == pending_buffers.end()) {
            next_buffer = pending_buffers.emplace(pending_buffers.end());
        }

        auto iter = next_buffer;

        ++next_buffer;

        return iter;
    }

    void free_pending_buffer(buffer_iterator iter) {
        assert(next_buffer != pending_buffers.begin());

        --next_buffer;
        if (iter != next_buffer) {
            std::swap(*iter, *next_buffer);
        }
    }

    buffer_list pending_buffers;
    buffer_iterator next_buffer;
};

template <typename... Channels>
class server_context : public base_context<server_context<Channels...>> {
public:
    friend class connection<server_context>;

    using typename base_context<server_context>::connection_type;
    using connection_map = std::map<asio::ip::udp::endpoint, std::shared_ptr<connection_type>>;
    using connection_iterator = typename connection_map::iterator;

    server_context(asio::io_context& io, int port) : io(&io), port(port), socket(io, asio::ip::udp::endpoint(asio::ip::udp::v4(), port)) {}

    void listen() {
        receive();
    }

private:
    void receive() {
        socket.async_receive_from(asio::buffer(buffer), sender_endpoint, [this](asio::error_code ec, std::size_t size) {
            auto type = message_type{};

            std::memcpy(&type, buffer.data(), sizeof(message_type));

            auto iter = active_connections.find(sender_endpoint);

            switch (type) {
                case message_type::CONNECT: {            
                    if (iter != active_connections.end()) {
                        iter->second->disconnect();
                        active_connections.erase(iter);
                    }
                    iter = active_connections.emplace(sender_endpoint, std::make_shared<connection_type>(*this, socket, sender_endpoint)).first;
                    break;
                }
                case message_type::DISCONNECT: {
                    if (iter != active_connections.end()) {
                        kill(iter);
                    } else {
                        std::cerr << "Unexpected DISCONNECT from unknown client " << sender_endpoint << std::endl;
                    }
                    break;
                }
                case message_type::CONNECT_OK: {
                    std::cerr << "Unexpected CONNECT_OK from client " << sender_endpoint << std::endl;
                    break;
                }
                case message_type::DATA: {
                    auto data = std::string(buffer.data() + sizeof(message_type), size - sizeof(message_type));

                    std::cout << "Recieved data \"" << data << "\" from " << sender_endpoint << std::endl;

                    if (iter != active_connections.end() && data == "ping") {
                        iter->second->send_data("pong");
                    }

                    break;
                }
            }

            receive();
        });
    }

    void kill(connection_type& conn) {
        kill(active_connections.find(conn.get_endpoint()));
    }

    void kill(connection_iterator iter) {
        if (iter != active_connections.end()) {
            active_connections.erase(iter);
        }
    }

    asio::io_context* io;
    int port;
    asio::ip::udp::socket socket;

    connection_map active_connections;

    asio::ip::udp::endpoint sender_endpoint;
    std::array<char, 64*1024> buffer;
};

template <typename... Channels>
class client_context : public base_context<client_context<Channels...>> {
public:
    friend class connection<client_context>;

    using typename base_context<client_context>::connection_type;

    client_context(asio::io_context& io, const asio::ip::udp::endpoint& client_endpoint, const asio::ip::udp::endpoint& server_endpoint) :
        io(&io),
        client_endpoint(client_endpoint),
        server_endpoint(server_endpoint),
        socket(io, client_endpoint),
        conn(nullptr) {}
    
    auto connect() -> std::weak_ptr<connection_type> {
        conn = std::make_shared<connection_type>(*this, socket, server_endpoint);
        conn->send_connect();
        receive();

        return std::weak_ptr(conn);
    }

    auto get_connection() -> std::weak_ptr<connection_type> {
        if (conn) {
            return std::weak_ptr(conn);
        }
        return {};
    }

private:
    void receive() {
        socket.async_receive_from(asio::buffer(buffer), sender_endpoint, [this](asio::error_code ec, std::size_t size) {
            if (sender_endpoint != server_endpoint) {
                std::cerr << "Receive datagram from unknown peer " << sender_endpoint << std::endl;
                if (conn->connected) {
                    receive();
                }
                return;
            }

            auto type = message_type{};

            std::memcpy(&type, buffer.data(), sizeof(message_type));

            switch (type) {
                case message_type::CONNECT_OK: {            
                    std::cerr << "Connect OK " << sender_endpoint << std::endl;
                    break;
                }
                case message_type::DISCONNECT: {
                    if (conn) {
                        conn->connected = false;
                        conn = nullptr;
                    }
                    break;
                }
                case message_type::CONNECT: {
                    std::cerr << "Unexpected CONNECT" << sender_endpoint << std::endl;
                    break;
                }
                case message_type::DATA: {
                    auto data = std::string(buffer.data() + sizeof(message_type), size - sizeof(message_type));

                    std::cout << "Recieved data \"" << data << "\" from " << sender_endpoint << std::endl;

                    if (conn && data == "ping") {
                        conn->send_data("pong");
                    }

                    break;
                }
            }

            if (conn && conn->connected) {
                receive();
            }
        });
    }

    void kill(connection_type& c) {
        assert(&c == conn.get());
        conn = nullptr;
    }

    asio::io_context* io;
    asio::ip::udp::endpoint client_endpoint;
    asio::ip::udp::endpoint server_endpoint;
    asio::ip::udp::socket socket;
    std::tuple<channel_state<Channels>...> channels;

    std::shared_ptr<connection_type> conn;

    asio::ip::udp::endpoint sender_endpoint;
    std::array<char, 64*1024> buffer;
};

} // namespace trellis
