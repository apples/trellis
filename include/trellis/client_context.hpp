#pragma once

#include "context_crtp.hpp"
#include "connection.hpp"
#include "datagram.hpp"
#include "message_header.hpp"

#include <asio.hpp>

#include <cstring>
#include <cassert>
#include <iostream>
#include <memory>

namespace trellis {

/** A client context that connects to only one server. */
template <typename... Channels>
class client_context final : public context_crtp<client_context<Channels...>> {
public:
    using base_type = context_crtp<client_context>;
    using connection_type = connection<client_context>;

    friend base_type;
    friend connection_type;

    using typename base_type::connection_ptr;
    using typename base_type::protocol;
    using typename base_type::traits;

    using base_type::get_context_id;

    /** Constructs a context running on the given io_context. */
    client_context(asio::io_context& io) :
        base_type(io),
        conn(nullptr) {}

    /** Connects the client to a server. Both the client and server need to have the same channel list. */
    void connect(const typename protocol::endpoint& client_endpoint, const typename protocol::endpoint& server_endpoint) {
        // must be executed from user thread
        assert(!this->is_thread_current());

        this->open(client_endpoint);
        conn = std::make_shared<connection_type>(*this, server_endpoint);

        this->dispatch([conn = conn]{
            conn->send_connect();
        });
    }

    /** Gets the client's local endpoint. */
    auto get_endpoint() const -> typename protocol::endpoint {
        return this->get_socket().local_endpoint();
    }

protected:
    virtual void kill(const connection_base& c, const asio::error_code& ec) override {
        // should only be called from a connection's methods, so we should be in the networking thread
        assert(this->is_thread_current());

        assert(&c == conn.get());
        TRELLIS_LOG_ACTION("client", get_context_id(), "Killing connection to ", conn->get_endpoint());

        this->push_event(_detail::event_disconnect{conn, ec});

        conn = nullptr;
        this->stop();
    }

    virtual void connection_error(const connection_base& c, asio::error_code ec) override {
        // should only be called from send and receive handlers, so we should be in the networking thread
        assert(this->is_thread_current());

        if (conn && &c == conn.get()) {
            connection_error(conn, ec);
        }
    }

private:
    /** Disconnects the connection to the server and closes the client socket. */
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

    void receive(const _detail::datagram_buffer& buffer, const typename protocol::endpoint& sender_endpoint, std::size_t size) {
        TRELLIS_BEGIN_SECTION("client");

        // should only be called from the base receive handler, so we should be in the networking thread
        assert(this->is_thread_current());

        assert(conn);

        if (sender_endpoint != conn->get_endpoint()) {
            TRELLIS_LOG_ACTION("client", get_context_id(), "Unexpected datagram from unknown peer ", sender_endpoint, ". Ignoring.");

            return;
        }

        assert(sender_endpoint == conn->get_endpoint());

        auto type = _detail::headers::type{};

        std::memcpy(&type, buffer.data.data(), sizeof(_detail::headers::type));

        switch (type) {
            case _detail::headers::type::CONNECT: {
                TRELLIS_LOG_ACTION("client", get_context_id(), "Unexpected CONNECT from server ", sender_endpoint, ". Disconnecting.");

                conn->disconnect();
                break;
            }
            case _detail::headers::type::CONNECT_OK: {
                auto header = _detail::headers::connect_ok{};
                std::memcpy(&header, buffer.data.data() + sizeof(type), sizeof(_detail::headers::connect_ok));

                TRELLIS_LOG_ACTION("client", get_context_id(), "CONNECT_OK (scid:", header.connection_id, ") from server ", sender_endpoint, ".");

                conn->receive_connect_ok(header, [this] {
                    TRELLIS_LOG_ACTION("client", get_context_id(), "CONNECT_OK caused connection to become ESTABLISHED. Pushing event_connect.");
                    this->push_event(_detail::event_connect{conn});
                });

                break;
            }
            case _detail::headers::type::CONNECT_ACK: {
                TRELLIS_LOG_ACTION("client", get_context_id(), "Unexpected CONNECT_ACK from server ", sender_endpoint, ". Disconnecting.");

                conn->disconnect();
                break;
            }
            case _detail::headers::type::DISCONNECT: {
                TRELLIS_LOG_ACTION("client", get_context_id(), "DISCONNECT from server ", sender_endpoint, ". Disconnecting without response.");

                conn->disconnect_without_send({});
                conn = nullptr;
                break;
            }
            case _detail::headers::type::DATA: {
                if (conn->get_state() != connection_state::ESTABLISHED) {
                    TRELLIS_LOG_ACTION("client", get_context_id(), "DATA received from server ", sender_endpoint, " before being ESTABLISHED. Disconnecting.");

                    conn->disconnect();
                    break;
                }

                auto header = _detail::headers::data{};
                std::memcpy(&header, buffer.data.data() + sizeof(_detail::headers::type), sizeof(_detail::headers::data));

                if (header.channel_id >= sizeof...(Channels)) {
                    TRELLIS_LOG_ACTION("client", get_context_id(), "DATA received with invalid channel_id. Disconnecting.");

                    conn->disconnect();
                    break;
                }

                conn->receive(header, buffer, size, [&](_detail::raw_buffer&& data) {
                    this->push_event(_detail::event_receive{conn, header.channel_id, std::move(data)});
                }, [&] {
                    // This should be unreachable, since we check for ESTABLISHED above.
                    TRELLIS_LOG_ACTION("client", get_context_id(), "DATA caused connection to become ESTABLISHED. That's not supposed to happen. Disconnecting.");
                    conn->disconnect();
                    assert(false);
                });

                break;
            }
            case _detail::headers::type::DATA_ACK: {
                if (conn->get_state() != connection_state::ESTABLISHED) {
                    TRELLIS_LOG_ACTION("client", get_context_id(), "DATA_ACK received from server ", sender_endpoint, " before being ESTABLISHED. Disconnecting.");

                    conn->disconnect();
                    break;
                }

                auto header = _detail::headers::data_ack{};
                std::memcpy(&header, buffer.data.data() + sizeof(_detail::headers::type), sizeof(_detail::headers::data_ack));

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

    void connection_error(const typename protocol::endpoint& endpoint, asio::error_code ec) {
        // should only be called from send and receive handlers, so we should be in the networking thread
        assert(this->is_thread_current());

        if (conn && endpoint == conn->get_endpoint()) {
            connection_error(conn, ec);
        }
    }

    void connection_error(const std::shared_ptr<connection_type>& ptr, asio::error_code ec) {
        // should only be called from send and receive handlers, so we should be in the networking thread
        assert(this->is_thread_current());

        assert(ptr);
        assert(ec);
        ptr->disconnect_without_send(ec);
    }

    std::shared_ptr<connection_type> conn;
};

} // namespace trellis
