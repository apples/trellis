#pragma once

#include "config.hpp"
#include "connection.hpp"
#include "channel.hpp"
#include "datagram.hpp"
#include "logging.hpp"
#include "message_header.hpp"
#include "streams.hpp"
#include "utility.hpp"

#include <asio.hpp>
#include <cereal/archives/binary.hpp>

#include <cstring>
#include <cassert>
#include <array>
#include <iostream>
#include <streambuf>
#include <tuple>
#include <memory>
#include <map>

#if TRELLIS_ENABLE_LOGGING
#include <iomanip>
#endif

namespace trellis {

template <typename C>
class base_context {
public:
    using connection_type = connection<C>;

    using buffer_iterator = datagram_buffer_cache::buffer_iterator;

    base_context() : cache() {}

    auto make_pending_buffer() -> buffer_iterator {
        return cache.make_pending_buffer();
    }

    void free_pending_buffer(buffer_iterator iter) {
        cache.free_pending_buffer(iter);
    }

private:
    datagram_buffer_cache cache;
};

template <typename... Channels>
class server_context : public base_context<server_context<Channels...>> {
public:
    friend class connection<server_context>;

    using typename base_context<server_context>::connection_type;
    using connection_map = std::map<asio::ip::udp::endpoint, std::shared_ptr<connection_type>>;
    using connection_iterator = typename connection_map::iterator;
    using receive_function = std::function<void(server_context&, const std::shared_ptr<connection_type>&, std::istream&)>;

    server_context(asio::io_context& io, int port) :
        io(&io),
        port(port),
        socket(io, asio::ip::udp::endpoint(asio::ip::udp::v4(), port)),
        receive_funcs(),
        active_connections(),
        sender_endpoint(),
        buffer() {}

    void listen() {
        receive();
    }

    template <typename C>
    void on_receive(receive_function func) {
        receive_funcs[index_of_v<C, Channels...>] = std::move(func);
    }

private:
    void receive() {
        socket.async_receive_from(asio::buffer(buffer), sender_endpoint, [this](asio::error_code ec, std::size_t size) {
            TRELLIS_LOG_DATAGRAM("recv", buffer, size);

            auto type = headers::type{};

            std::memcpy(&type, buffer.data(), sizeof(headers::type));

            auto iter = active_connections.find(sender_endpoint);

            switch (type) {
                case headers::type::CONNECT: {            
                    if (iter != active_connections.end()) {
                        iter->second->disconnect();
                        active_connections.erase(iter);
                    }
                    iter = active_connections.emplace(sender_endpoint, std::make_shared<connection_type>(*this, socket, sender_endpoint)).first;
                    iter->second->send_connect_ok();
                    break;
                }
                case headers::type::DISCONNECT: {
                    if (iter != active_connections.end()) {
                        kill(iter);
                    } else {
                        std::cerr << "Unexpected DISCONNECT from unknown client " << sender_endpoint << std::endl;
                    }
                    break;
                }
                case headers::type::CONNECT_OK: {
                    std::cerr << "Unexpected CONNECT_OK from client " << sender_endpoint << std::endl;
                    break;
                }
                case headers::type::DATA: {
                    if (iter != active_connections.end()) {
                        auto& conn = iter->second;
                        auto header = headers::data{};
                        std::memcpy(&header, buffer.data() + sizeof(headers::type), sizeof(headers::data));
                        assert(header.channel_id < sizeof...(Channels));
                        assert(header.channel_id < receive_funcs.size());

                        auto& func = receive_funcs[header.channel_id];

                        if (!func) {
                            std::cerr << "Warning: No receive handler for channel_id " << header.channel_id << std::endl;
                            func = [](server_context&, const std::shared_ptr<connection_type>&, std::istream&) {};
                        }

                        conn->receive(header, buffer, size, [&](std::istream& s) {
                            func(*this, conn, s);
                        });
                    } else {
                        std::cerr << "Unexpected DATA from unknown client" << std::endl;
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

    std::array<receive_function, sizeof...(Channels)> receive_funcs;
    connection_map active_connections;

    asio::ip::udp::endpoint sender_endpoint;
    datagram_buffer buffer;
};

template <typename... Channels>
class client_context : public base_context<client_context<Channels...>> {
public:
    friend class connection<client_context>;

    using typename base_context<client_context>::connection_type;
    using connect_function = std::function<void(client_context&, const std::shared_ptr<connection_type>&)>;
    using receive_function = std::function<void(client_context&, const std::shared_ptr<connection_type>&, std::istream&)>;

    client_context(asio::io_context& io, const asio::ip::udp::endpoint& client_endpoint, const asio::ip::udp::endpoint& server_endpoint) :
        io(&io),
        client_endpoint(client_endpoint),
        server_endpoint(server_endpoint),
        socket(io, client_endpoint),
        channels(),
        on_connect_func(),
        receive_funcs(),
        conn(nullptr),
        sender_endpoint(),
        buffer() {}
    
    void connect(connect_function func) {
        assert(!on_connect_func);
        on_connect_func = func;
        conn = std::make_shared<connection_type>(*this, socket, server_endpoint);
        conn->send_connect();
        receive();
    }

    auto get_connection() -> std::weak_ptr<connection_type> {
        if (conn) {
            return std::weak_ptr(conn);
        }
        return {};
    }

    template <typename C>
    void on_receive(receive_function func) {
        receive_funcs[index_of_v<C, Channels...>] = std::move(func);
    }

private:
    void receive() {
        socket.async_receive_from(asio::buffer(buffer), sender_endpoint, [this](asio::error_code ec, std::size_t size) {
            TRELLIS_LOG_DATAGRAM("recv", buffer, size);

            if (sender_endpoint != server_endpoint) {
                std::cerr << "Receive datagram from unknown peer " << sender_endpoint << std::endl;
                if (conn->connected) {
                    receive();
                }
                return;
            }

            auto type = headers::type{};

            std::memcpy(&type, buffer.data(), sizeof(headers::type));

            switch (type) {
                case headers::type::CONNECT_OK: {            
                    assert(on_connect_func);
                    on_connect_func(*this, conn);
                    on_connect_func = {};
                    break;
                }
                case headers::type::DISCONNECT: {
                    if (conn) {
                        conn->connected = false;
                        conn = nullptr;
                    }
                    break;
                }
                case headers::type::CONNECT: {
                    std::cerr << "Unexpected CONNECT" << sender_endpoint << std::endl;
                    break;
                }
                case headers::type::DATA: {
                    assert(conn);

                    auto header = headers::data{};
                    std::memcpy(&header, buffer.data() + sizeof(headers::type), sizeof(headers::data));
                    assert(header.channel_id < sizeof...(Channels));
                    assert(header.channel_id < receive_funcs.size());

                    auto& func = receive_funcs[header.channel_id];

                    if (!func) {
                        std::cerr << "Warning: No receive handler for channel_id " << header.channel_id << std::endl;
                        func = [](client_context&, const std::shared_ptr<connection_type>&, std::istream&) {};
                    }

                    conn->receive(header, buffer, size, [&](std::istream& s) {
                        func(*this, conn, s);
                    });

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
    std::tuple<channel<Channels>...> channels;

    connect_function on_connect_func;
    std::array<receive_function, sizeof...(Channels)> receive_funcs;
    std::shared_ptr<connection_type> conn;

    asio::ip::udp::endpoint sender_endpoint;
    datagram_buffer buffer;
};

} // namespace trellis
