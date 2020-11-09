#pragma once

#include "base_context.hpp"
#include "connection.hpp"
#include "datagram.hpp"
#include "message_header.hpp"

#include <asio.hpp>

#include <cstring>
#include <cassert>
#include <iostream>
#include <memory>

namespace trellis {

template <typename... Channels>
class client_context : public base_context<client_context<Channels...>> {
public:
    using base_type = base_context<client_context>;
    using typename base_type::connection_type;

    friend base_type;
    friend connection_type;

    using typename base_type::protocol;

    using connect_function = std::function<void(client_context&, const std::shared_ptr<connection_type>&)>;
    using receive_function = std::function<void(client_context&, const std::shared_ptr<connection_type>&, std::istream&)>;

    client_context(asio::io_context& io) :
        base_type(io),
        on_connect_func(),
        conn(nullptr) {}
    
    void connect(const typename protocol::endpoint& client_endpoint, const typename protocol::endpoint& server_endpoint, connect_function func) {
        assert(!on_connect_func);
        assert(func);
        on_connect_func = func;
        this->open(client_endpoint);
        conn = std::make_shared<connection_type>(*this, this->get_socket(), server_endpoint);
        conn->send_connect();
    }

    void disconnect_all() {
        if (conn) {
            conn->disconnect([this] {
                assert(!conn);
                this->close();
            });
        } else {
            this->close();
        }
    }

private:
    void receive(const datagram_buffer& buffer, const typename protocol::endpoint& sender_endpoint, std::size_t size) {
        assert(conn);

        if (sender_endpoint != conn->get_endpoint()) {
            std::cerr << "Receive datagram from unknown peer " << sender_endpoint << std::endl;
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
                auto header = headers::data{};
                std::memcpy(&header, buffer.data() + sizeof(headers::type), sizeof(headers::data));
                assert(header.channel_id < sizeof...(Channels));

                auto& func = this->get_receive_func(header.channel_id);

                conn->receive(header, buffer, size, [&](std::istream& s) {
                    func(*this, conn, s);
                });

                break;
            }
        }
    }

    void kill(connection_type& c) {
        assert(&c == conn.get());
        conn = nullptr;
        this->stop();
    }

    connect_function on_connect_func;
    std::shared_ptr<connection_type> conn;
};

} // namespace trellis
