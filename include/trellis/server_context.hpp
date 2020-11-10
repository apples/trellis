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
#include <map>

namespace trellis {

template <typename... Channels>
class server_context : public base_context<server_context<Channels...>> {
public:
    using base_type = base_context<server_context>;
    using typename base_type::connection_type;

    friend base_type;
    friend connection_type;

    using typename base_type::connection_ptr;
    using typename base_type::protocol;

    using connection_map = std::map<typename protocol::endpoint, std::shared_ptr<connection_type>>;

    server_context(asio::io_context& io) :
        base_type(io),
        active_connections() {}

    void listen(const typename protocol::endpoint& endpoint) {
        this->open(endpoint);
    }

    auto get_endpoint() const -> typename protocol::endpoint {
        return this->get_socket().local_endpoint();
    }

    void disconnect_all() {
        for (auto& [endpoint, conn] : active_connections) {
            conn->disconnect([this] {
                if (active_connections.size() == 0) {
                    this->close();
                }
            });
        }
    }

private:
    void receive(const datagram_buffer& buffer, const typename protocol::endpoint& sender_endpoint, std::size_t size) {
        auto type = headers::type{};

        std::memcpy(&type, buffer.data(), sizeof(headers::type));

        auto iter = active_connections.find(sender_endpoint);

        switch (type) {
            case headers::type::CONNECT: {            
                if (iter != active_connections.end()) {
                    iter->second->disconnect();
                    active_connections.erase(iter);
                }
                auto [ins_iter, success] = active_connections.emplace(sender_endpoint, std::make_shared<connection_type>(*this, this->get_socket(), sender_endpoint));
                assert(success);
                ins_iter->second->send_connect_ok();
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

                    auto& func = this->get_receive_func(header.channel_id);

                    conn->receive(header, buffer, size, [&](std::istream& s) {
                        func(*this, conn, s);
                    });
                } else {
                    std::cerr << "Unexpected DATA from unknown client" << std::endl;
                }

                break;
            }
        }
    }

    void kill(connection_type& conn) {
        kill(active_connections.find(conn.get_endpoint()));
    }

    void kill(const typename connection_map::iterator& iter) {
        if (iter != active_connections.end()) {
            active_connections.erase(iter);
        }
    }

    connection_map active_connections;
};

} // namespace trellis
