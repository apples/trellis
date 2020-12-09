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
    using connection_type = connection<client_context>;

    friend base_type;
    friend connection_type;

    using typename base_type::connection_ptr;
    using typename base_type::protocol;
    using typename base_type::traits;

    using connect_function = std::function<void(client_context&, const std::shared_ptr<connection_type>&)>;
    using receive_function = std::function<void(client_context&, const std::shared_ptr<connection_type>&, std::istream&)>;

    using base_type::get_context_id;

    client_context(asio::io_context& io) :
        base_type(io),
        on_connect_func(),
        conn(nullptr) {}

    void connect(const typename protocol::endpoint& client_endpoint, const typename protocol::endpoint& server_endpoint) {
        assert(!on_connect_func);

        this->open(client_endpoint);
        conn = std::make_shared<connection_type>(*this, server_endpoint);
        conn->send_connect();
    }

    auto get_endpoint() const -> typename protocol::endpoint {
        return this->get_socket().local_endpoint();
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

    void on_connect(connect_function func) {
        on_connect_func = std::move(func);
    }

private:
    void receive(const datagram_buffer& buffer, const typename protocol::endpoint& sender_endpoint, std::size_t size) {
        TRELLIS_BEGIN_SECTION("client");

        assert(conn);

        if (sender_endpoint != conn->get_endpoint()) {
            TRELLIS_LOG_ACTION("client", get_context_id(), "Unexpected datagram from unknown peer ", sender_endpoint, ". Ignoring.");

            return;
        }

        assert(sender_endpoint == conn->get_endpoint());

        auto type = headers::type{};

        std::memcpy(&type, buffer.data.data(), sizeof(headers::type));

        switch (type) {
            case headers::type::CONNECT: {
                TRELLIS_LOG_ACTION("client", get_context_id(), "Unexpected CONNECT from server ", sender_endpoint, ". Disconnecting.");

                conn->disconnect();
                break;
            }
            case headers::type::CONNECT_OK: {
                auto header = headers::connect_ok{};
                std::memcpy(&header, buffer.data.data() + sizeof(type), sizeof(headers::connect_ok));

                TRELLIS_LOG_ACTION("client", get_context_id(), "CONNECT_OK (scid:", header.connection_id, ") from server ", sender_endpoint, ".");

                conn->receive_connect_ok(header, [this] {
                    TRELLIS_LOG_ACTION("client", get_context_id(), "CONNECT_OK caused connection to become ESTABLISHED. Calling on_connect callback.");
                    if (on_connect_func) {
                        on_connect_func(*this, conn);
                    }
                });

                break;
            }
            case headers::type::CONNECT_ACK: {
                TRELLIS_LOG_ACTION("client", get_context_id(), "Unexpected CONNECT_ACK from server ", sender_endpoint, ". Disconnecting.");

                conn->disconnect();
                break;
            }
            case headers::type::DISCONNECT: {
                TRELLIS_LOG_ACTION("client", get_context_id(), "DISCONNECT from server ", sender_endpoint, ". Disconnecting without response.");

                conn->disconnect_without_send();
                conn = nullptr;
                break;
            }
            case headers::type::DATA: {
                if (conn->get_state() != connection_state::ESTABLISHED) {
                    TRELLIS_LOG_ACTION("client", get_context_id(), "DATA received from server ", sender_endpoint, " before being ESTABLISHED. Disconnecting.");

                    conn->disconnect();
                    break;
                }

                auto header = headers::data{};
                std::memcpy(&header, buffer.data.data() + sizeof(headers::type), sizeof(headers::data));

                if (header.channel_id >= sizeof...(Channels)) {
                    TRELLIS_LOG_ACTION("client", get_context_id(), "DATA received with invalid channel_id. Disconnecting.");

                    conn->disconnect();
                    break;
                }

                auto& receive_func = this->get_receive_func(header.channel_id);

                conn->receive(header, buffer, size, [this, &receive_func](std::istream& s) {
                    receive_func(*this, conn, s);
                }, [this] {
                    // This should be unreachable, since we check for ESTABLISHED above.
                    TRELLIS_LOG_ACTION("client", get_context_id(), "DATA caused connection to become ESTABLISHED. That's not supposed to happen. Disconnecting.");
                    conn->disconnect();
                    assert(false);
                });

                break;
            }
            case headers::type::DATA_ACK: {
                if (conn->get_state() != connection_state::ESTABLISHED) {
                    TRELLIS_LOG_ACTION("client", get_context_id(), "DATA_ACK received from server ", sender_endpoint, " before being ESTABLISHED. Disconnecting.");

                    conn->disconnect();
                    break;
                }

                auto header = headers::data_ack{};
                std::memcpy(&header, buffer.data.data() + sizeof(headers::type), sizeof(headers::data_ack));

                if (header.channel_id >= sizeof...(Channels)) {
                    TRELLIS_LOG_ACTION("client", get_context_id(), "DATA_ACK received with invalid channel_id. Disconnecting.");

                    conn->disconnect();
                    break;
                }

                conn->receive_ack(header);

                break;
            }
        }

        TRELLIS_END_SECTION("client");
    }

    void kill(connection_type& c) {
        assert(&c == conn.get());
        TRELLIS_LOG_ACTION("client", get_context_id(), "Killing connection to ", conn->get_endpoint());

        conn = nullptr;
        this->stop();
    }

    connect_function on_connect_func;
    std::shared_ptr<connection_type> conn;
};

} // namespace trellis
