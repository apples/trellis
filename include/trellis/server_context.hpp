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

    using base_type::get_context_id;

    /** Constructs a context from the given io_context. */
    server_context(asio::io_context& io) :
        base_type(io),
        active_connections() {}

    /** Opens the server socket to listen for incoming client connections. Both the server and the clients must have matching channel lists. */
    void listen(const typename protocol::endpoint& endpoint) {
        // must be executed from user thread
        assert(!this->is_thread_current());

        this->open(endpoint);
    }

    /** Gets the server's local endpoint. */
    auto get_endpoint() const -> typename protocol::endpoint {
        return this->get_socket().local_endpoint();
    }

protected:
    virtual void kill(const connection_base& conn, const asio::error_code& ec) override {
        // should only be called from a connection's methods, so we should be in the networking thread
        assert(this->is_thread_current());

        auto endpoint = conn.get_endpoint();
        auto iter = active_connections.find(endpoint);
        kill_iter(iter, ec);
    }

    virtual void connection_error(const connection_base& c, asio::error_code ec) override {
        // should only be called from send and receive handlers, so we should be in the networking thread
        assert(this->is_thread_current());

        auto iter = active_connections.find(c.get_endpoint());
        if (iter != active_connections.end()) {
            connection_error(iter->second, ec);
        }
    }

private:
    /** Disconnects all client connections and closes the socket. */
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

    void kill_iter(typename connection_map::iterator iter, const asio::error_code& ec) {
        // should only be called from kill(conn) and the receive handler, so we should be in the networking thread
        assert(this->is_thread_current());

        // By contract, this function should never be called with an invalid iterator.
        assert(iter != active_connections.end());

        TRELLIS_LOG_ACTION("server", get_context_id(), "Killing connection ", iter->second->get_endpoint());

        this->push_event(_detail::event_disconnect{iter->second, ec});

        active_connections.erase(iter);
    }

    void receive(const _detail::datagram_buffer& buffer, const typename protocol::endpoint& sender_endpoint, std::size_t size) {
        TRELLIS_BEGIN_SECTION("server");

        // should only be called from the base receive handler, so we should be in the networking thread
        assert(this->is_thread_current());

        auto type = _detail::headers::type{};

        std::memcpy(&type, buffer.data.data(), sizeof(_detail::headers::type));

        auto iter = active_connections.find(sender_endpoint);

        switch (type) {
            case _detail::headers::type::CONNECT: {
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
            case _detail::headers::type::CONNECT_OK: {
                if (iter != active_connections.end()) {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "Unexpected CONNECT_OK from client ", sender_endpoint, ". Disconnecting.");

                    iter->second->disconnect();
                } else {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "Unexpected CONNECT_OK from unknown client ", sender_endpoint, ". Ignoring.");
                }
                break;
            }
            case _detail::headers::type::CONNECT_ACK: {
                if (iter != active_connections.end()) {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "CONNECT_ACK from client ", sender_endpoint, ".");

                    iter->second->receive_connect_ack([this, &iter] {
                        TRELLIS_LOG_ACTION("server", get_context_id(), "CONNECT_ACK caused connection to become ESTABLISHED. Pushing event_connect.");
                        this->push_event(_detail::event_connect{iter->second});
                    });
                } else {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "Unexpected CONNECT_ACK from unknown client ", sender_endpoint);
                }
                break;
            }
            case _detail::headers::type::DISCONNECT: {
                if (iter != active_connections.end()) {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "DISCONNECT from client ", sender_endpoint, ". Killing connection.");

                    kill_iter(iter, {});
                } else {
                    TRELLIS_LOG_ACTION("server", get_context_id(), "Unexpected DISCONNECT from unknown client ", sender_endpoint);
                }
                break;
            }
            case _detail::headers::type::DATA: {
                if (iter != active_connections.end()) {
                    auto& conn = iter->second;

                    if (conn->get_state() == connection_state::PENDING || conn->get_state() == connection_state::ESTABLISHED) {
                        auto header = _detail::headers::data{};
                        std::memcpy(&header, buffer.data.data() + sizeof(_detail::headers::type), sizeof(_detail::headers::data));

                        if (header.channel_id >= sizeof...(Channels)) {
                            TRELLIS_LOG_ACTION("server", get_context_id(), "DATA received with invalid channel_id. Disconnecting.");

                            conn->disconnect();
                            break;
                        }

                        TRELLIS_LOG_FRAGMENT("server", +header.fragment_id, +header.fragment_count);

                        conn->receive(header, buffer, size, [&](_detail::raw_buffer&& data) {
                            this->push_event(_detail::event_receive{conn, header.channel_id, std::move(data)});
                        }, [&] {
                            TRELLIS_LOG_ACTION("server", get_context_id(), "DATA caused connection to become ESTABLISHED. Pushing event_connect.");
                            this->push_event(_detail::event_connect{iter->second});
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
            case _detail::headers::type::DATA_ACK: {
                if (iter != active_connections.end()) {
                    auto& conn = iter->second;

                    if (conn->get_state() == connection_state::ESTABLISHED) {
                        auto header = _detail::headers::data_ack{};
                        std::memcpy(&header, buffer.data.data() + sizeof(_detail::headers::type), sizeof(_detail::headers::data_ack));

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
        // should only be called from send and receive handlers, so we should be in the networking thread
        assert(this->is_thread_current());

        auto iter = active_connections.find(endpoint);
        if (iter != active_connections.end()) {
            connection_error(iter->second, ec);
        }
    }

    void connection_error(const connection_ptr& ptr, asio::error_code ec) {
        // should only be called from send and receive handlers, so we should be in the networking thread
        assert(this->is_thread_current());

        assert(ptr);
        assert(ec);
        ptr->disconnect_without_send(ec);
    }

    connection_map active_connections;
};

} // namespace trellis
