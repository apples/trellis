#pragma once

#include "context_crtp.hpp"
#include "channel.hpp"
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

/** A server implementation that can have any number of connected clients. */
template <typename... Channels>
class server_context final : public context_crtp<server_context<Channels...>> {
public:
    using base_type = context_crtp<server_context>;
    using connection_type = connection<server_context>;

    friend base_type;
    friend connection_type;

    using typename base_type::connection_ptr;
    using typename base_type::protocol;
    using typename base_type::traits;

    using connection_map = std::map<typename protocol::endpoint, std::shared_ptr<connection_type>>;

    using connect_function = std::function<void(server_context&, const std::shared_ptr<connection_type>&)>;
    using disconnect_function = std::function<void(server_context&, const std::shared_ptr<connection_type>&, asio::error_code)>;

    using base_type::get_context_id;

    server_context(asio::io_context& io) :
        base_type(io),
        active_connections(),
        on_connect_func(),
        on_disconnect_func() {}

    void listen(const typename protocol::endpoint& endpoint) {
        this->open(endpoint);
    }

    auto get_endpoint() const -> typename protocol::endpoint {
        return this->get_socket().local_endpoint();
    }

    void disconnect_all() {
        if (active_connections.empty()) {
            this->close();
        } else {
            for (auto& [endpoint, conn] : active_connections) {
                conn->disconnect([this] {
                    if (active_connections.size() == 0) {
                        this->close();
                    }
                });
            }
        }
    }

    void on_connect(connect_function func) {
        on_connect_func = std::move(func);
    }

    void on_disconnect(disconnect_function func) {
        on_disconnect_func = std::move(func);
    }

protected:
    virtual void kill(const connection_base& conn) override {
        auto endpoint = conn.get_endpoint();
        auto iter = active_connections.find(endpoint);
        kill_iter(iter);
    }

    virtual void connection_error(const connection_base& c, asio::error_code ec) override {
        auto iter = active_connections.find(c.get_endpoint());
        if (iter != active_connections.end()) {
            connection_error(iter->second, ec);
        }
    }

private:
    void kill_iter(typename connection_map::iterator iter) {
        // By contract, this function should never be called with an invalid iterator.
        assert(iter != active_connections.end());

        TRELLIS_LOG_ACTION("server", get_context_id(), "Killing connection ", iter->second->get_endpoint());

        active_connections.erase(iter);
    }

    void receive(const datagram_buffer& buffer, const typename protocol::endpoint& sender_endpoint, std::size_t size) {
        TRELLIS_BEGIN_SECTION("server");

        auto type = headers::type{};

        std::memcpy(&type, buffer.data.data(), sizeof(headers::type));

        auto iter = active_connections.find(sender_endpoint);

        switch (type) {
            case headers::type::CONNECT: {
                if (iter == active_connections.end()) {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "Received CONNECT for unknown connection. Creating connection.");

                    auto [ins_iter, success] = active_connections.emplace(sender_endpoint, std::make_shared<connection_type>(*this, sender_endpoint));

                    assert(success);

                    iter = ins_iter;
                }

                assert(iter != active_connections.end());

                if (iter->second->get_state() == connection_state::INACTIVE) {
                    // If the connection is ESTABLISHED, then the client must have responded to a CONNECT_OK, so this is probably a stray message.
                    if (iter->second->get_state() != connection_state::ESTABLISHED) {
                        TRELLIS_LOG_ACTION("server", get_context_id(), "Received CONNECT for INACTIVE connection. Sending CONNECT_OK.");

                        iter->second->send_connect_ok();
                    } else {
                        TRELLIS_LOG_ACTION("server", get_context_id(), "Unexpected CONNECT for ESTABLISHED connection ", sender_endpoint, ". Ignoring.");
                    }
                } else {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "Received CONNECT for active connection. Ignoring.");
                }
                break;
            }
            case headers::type::CONNECT_OK: {
                if (iter != active_connections.end()) {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "Unexpected CONNECT_OK from client ", sender_endpoint, ". Disconnecting.");

                    iter->second->disconnect();
                } else {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "Unexpected CONNECT_OK from unknown client ", sender_endpoint, ". Ignoring.");
                }
                break;
            }
            case headers::type::CONNECT_ACK: {
                if (iter != active_connections.end()) {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "CONNECT_ACK from client ", sender_endpoint, ".");

                    iter->second->receive_connect_ack([this, &iter] {
                        TRELLIS_LOG_ACTION("server", get_context_id(), "CONNECT_ACK caused connection to become ESTABLISHED. Calling on_connect callback.");
                        if (on_connect_func) {
                            on_connect_func(*this, iter->second);
                        }
                    });
                } else {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "Unexpected CONNECT_ACK from unknown client ", sender_endpoint);
                }
                break;
            }
            case headers::type::DISCONNECT: {
                if (iter != active_connections.end()) {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "DISCONNECT from client ", sender_endpoint, ". Killing connection.");

                    kill_iter(iter);
                } else {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "Unexpected DISCONNECT from unknown client ", sender_endpoint);
                }
                break;
            }
            case headers::type::DATA: {
                if (iter != active_connections.end()) {
                    auto& conn = iter->second;

                    if (conn->get_state() == connection_state::PENDING || conn->get_state() == connection_state::ESTABLISHED) {
                        auto header = headers::data{};
                        std::memcpy(&header, buffer.data.data() + sizeof(headers::type), sizeof(headers::data));

                        if (header.channel_id >= sizeof...(Channels)) {
                            TRELLIS_LOG_ACTION("server", get_context_id(), "DATA received with invalid channel_id. Disconnecting.");

                            conn->disconnect();
                            break;
                        }

                        auto& receive_func = this->get_receive_func(header.channel_id);

                        TRELLIS_LOG_FRAGMENT("server", +header.fragment_id, +header.fragment_count);

                        conn->receive(header, buffer, size, [this, &receive_func, &conn](std::istream& s) {
                            receive_func(*this, conn, s);
                        }, [this, &conn] {
                            TRELLIS_LOG_ACTION("server", get_context_id(), "DATA caused connection to become ESTABLISHED. Calling on_connect callback.");
                            if (on_connect_func) {
                                on_connect_func(*this, conn);
                            }
                        });
                    } else {
                        TRELLIS_LOG_ACTION("server", get_context_id(), "Unexpected DATA from client ", sender_endpoint, ", which has not completed the handshake. Disconnecting.");

                        conn->disconnect();
                    }
                } else {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "Unexpected DATA from unknown client ", sender_endpoint, ". Ignoring.");
                }

                break;
            }
            case headers::type::DATA_ACK: {
                if (iter != active_connections.end()) {
                    auto& conn = iter->second;

                    if (conn->get_state() == connection_state::ESTABLISHED) {
                        auto header = headers::data_ack{};
                        std::memcpy(&header, buffer.data.data() + sizeof(headers::type), sizeof(headers::data_ack));

                        TRELLIS_LOG_FRAGMENT("server", +header.fragment_id, "?");

                        if (header.channel_id >= sizeof...(Channels)) {
                            TRELLIS_LOG_ACTION("server", get_context_id(), "DATA_ACK received with invalid channel_id. Disconnecting.");

                            conn->disconnect();
                            break;
                        }

                        conn->receive_ack(header);
                    } else {
                        TRELLIS_LOG_ACTION("server", get_context_id(), "Unexpected DATA_ACK from client ", sender_endpoint, ", which has not completed the handshake. Disconnecting.");

                        conn->disconnect();
                    }
                } else {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "Unexpected DATA_ACK from unknown client ", sender_endpoint, ". Ignoring.");
                }

                break;
            }
        }

        TRELLIS_END_SECTION("server");
    }

    void connection_error(const typename protocol::endpoint& endpoint, asio::error_code ec) {
        auto iter = active_connections.find(endpoint);
        if (iter != active_connections.end()) {
            connection_error(iter->second, ec);
        }
    }

    void connection_error(const std::shared_ptr<connection_type>& ptr, asio::error_code ec) {
        assert(ptr);
        if (on_disconnect_func) {
            on_disconnect_func(*this, ptr, ec);
        }
        ptr->disconnect_without_send();
    }

    connection_map active_connections;
    connect_function on_connect_func;
    disconnect_function on_disconnect_func;
};

} // namespace trellis
