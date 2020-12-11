#pragma once

#include "context_base.hpp"
#include "logging.hpp"
#include "message_header.hpp"

#include <asio.hpp>

#include <memory>
#include <optional>
#include <iostream>
#include <random>
#include <type_traits>

namespace trellis {

enum class connection_state {
    /** Default state, connection is unavailable. */
    INACTIVE,
    /** Client side. CONNECT has been sent, awaiting CONNECT_OK. */
    CONNECTING,
    /** Server side. CONNECT_OK has been sent, awaiting CONNECT_ACK or DATA. For most cases, equivalent to ESTABLISHED. */
    PENDING,
    /** Connection fully established and acknowledged. */
    ESTABLISHED,
    /** Connection ended. Pending destruction. */
    DISCONNECTED,
};

class connection_base : public std::enable_shared_from_this<connection_base> {
public:
    friend class channel_unreliable;
    friend class channel_reliable;

    using protocol = context_base::protocol;
    using timer_type = asio::steady_timer;

    using std::enable_shared_from_this<connection_base>::shared_from_this;

    connection_base(context_base& context, const protocol::endpoint& client_endpoint) :
        context(&context),
        client_endpoint(client_endpoint),
        state(connection_state::INACTIVE),
        connection_id(std::uniform_int_distribution<std::uint16_t>{}(context.get_rng())),
        handshake(std::nullopt) {
            TRELLIS_LOG_ACTION("conn", connection_id, "Connection constructed.");
        }

    connection_base(const connection_base&) = delete;
    connection_base(connection_base&&) = delete;

    auto get_context() -> context_base& {
        return *context;
    }

    auto get_endpoint() const -> const asio::ip::udp::endpoint& {
        return client_endpoint;
    }

    auto get_state() const -> connection_state {
        return state;
    }

    auto get_connection_id() -> std::uint16_t {
        return connection_id;
    }

    void disconnect() {
        disconnect([]{});
    }

    template <typename F>
    void disconnect(const F& func) {
        if (state == connection_state::DISCONNECTED) {
            TRELLIS_LOG_ACTION("conn", connection_id, "Attempted to disconnect an already DISCONNECTED connection.");
            return;
        }

        TRELLIS_LOG_ACTION("conn", connection_id, "Disconnecting.");

        state = connection_state::DISCONNECTED;

        auto buffer = context->make_pending_buffer();
        auto type = headers::type::DISCONNECT;
        std::memcpy(buffer.data(), &type, sizeof(type));

        TRELLIS_LOG_DATAGRAM("d/cn", buffer, sizeof(type));

        context->get_socket().async_send_to(
            buffer.buffer(sizeof(type)),
            client_endpoint,
            [buffer, func, self = this->shared_from_this()]([[maybe_unused]] asio::error_code ec, [[maybe_unused]] std::size_t size) {
                TRELLIS_LOG_ACTION("conn", self->connection_id, "Sent DISCONNECT successfully, killing connection.");
                self->context->kill(*self);
                func();
            });
    }

protected:
    void set_state(connection_state s) {
        state = s;
    }

    /** Sends a datagram. */
    void send_raw(const shared_datagram_buffer& data, std::size_t count) {
        if (state == connection_state::DISCONNECTED) return;

        TRELLIS_LOG_DATAGRAM("send_raw", data, count);

        context->get_socket().async_send_to(data.buffer(count), client_endpoint, [data, ptr = shared_from_this()](asio::error_code ec, [[maybe_unused]] std::size_t size) {
            if (ec) {
                std::cerr << "Error: " << ec.category().name() << ": " << ec.message() << std::endl;
                ptr->disconnect();
            }
        });
    }

    /**
     * First phase of handshake, client side.
     * Changes state from INACTIVE to CONNECTING, and sends CONNECT messages in a loop until receive_connect_ok() is called.
     */
    void send_connect() {
        if (!handshake) {
            TRELLIS_LOG_ACTION("conn", connection_id, "Client starting handshake. Now CONNECTING.");

            // Since send_connect() should only be called (externally) once per connection, we should be INACTIVE.
            assert(state == connection_state::INACTIVE);
            state = connection_state::CONNECTING;

            auto buffer = context->make_pending_buffer();
            auto type = headers::type::CONNECT;

            std::memcpy(buffer.data(), &type, sizeof(type));

            TRELLIS_LOG_ACTION("conn", connection_id, "Sending CONNECT.");
            send_raw(buffer, sizeof(type));

            handshake = handshake_state{
                asio::steady_timer(context->get_io()),
                buffer,
            };
        }

        // Should always be in CONNECTING state because the handshake is cancelled when we receive CONNECT_OK.
        assert(state == connection_state::CONNECTING);

        // 200ms for now, should probably be dynamic in the future.
        handshake->timer.expires_from_now(std::chrono::milliseconds{200});

        handshake->timer.async_wait([this](asio::error_code ec) {
            if (ec == asio::error::operation_aborted) {
                return;
            }

            // The only possible error is operation_aborted.
            assert(!ec);

            // Should be in the CONNECTING state because if we received a CONNECT_OK then this timer should have been cancelled.
            assert(state == connection_state::CONNECTING);

            TRELLIS_LOG_ACTION("conn", connection_id, "Resending CONNECT due to timeout.");
            send_raw(handshake->buffer, sizeof(headers::type));

            send_connect();
        });
    }

    /**
     * Second phase of handshake, client side.
     * Changes state from CONNECTING to ESTABLISHED if necessary, cancels handshake, and sends a CONNECT_ACK.
     * If becoming ESTABLISHED, calls on_establish. The callback is not stored, so feel free to capture locals by reference.
     * Only one CONNECT_ACK is sent, if it gets lost in transit, the server will keep sending us CONNECT_OK messages,
     * so we need to respond to each CONNECT_OK with a CONNECT_ACK.
     * The server will also stop sending CONNECT_OKs when we send our first DATA message.
     */
    template <typename F>
    void receive_connect_ok(const headers::connect_ok& connect_ok, const F& on_establish) {
        TRELLIS_LOG_ACTION("conn", connection_id, "Received CONNECT_OK (rcid:", connect_ok.connection_id, ").");

        // If state isn't CONNECTING then we don't change state, but still need to send CONNECT_ACK anyways.
        if (state == connection_state::CONNECTING) {
            TRELLIS_LOG_ACTION("conn", connection_id, "Established. Calling on_establish.");

            cancel_handshake();

            state = connection_state::ESTABLISHED;
            on_establish();
        }

        // Handshake should only ever exist during the CONNECTING state.
        assert(!handshake);

        auto buffer = context->make_pending_buffer();
        auto type = headers::type::CONNECT_ACK;

        // The connection_id needs to match the one in the CONNECT_OK message.
        auto header = headers::connect_ack{connect_ok.connection_id};

        constexpr auto size = sizeof(type) + sizeof(header);

        std::memcpy(buffer.data(), &type, sizeof(type));
        std::memcpy(buffer.data() + sizeof(type), &header, sizeof(header));

        send_raw(buffer, size);
    }

    /**
     * Second phase of handshake, server side.
     * Sends CONNECT_OK messages repeatedly until a CONNECT_ACK or DATA message is received.
     */
    void send_connect_ok() {
        if (!handshake) {
            // Since send_connect_ok() should only be called (externally) once per connection, we should be INACTIVE.
            assert(state == connection_state::INACTIVE);

            TRELLIS_LOG_ACTION("conn", connection_id, "Server starting handshake. Now PENDING.");

            state = connection_state::PENDING;

            auto buffer = context->make_pending_buffer();
            auto type = headers::type::CONNECT_OK;
            auto header = headers::connect_ok{connection_id};
            constexpr auto size = sizeof(type) + sizeof(header);

            std::memcpy(buffer.data(), &type, sizeof(type));
            std::memcpy(buffer.data() + sizeof(type), &header, sizeof(header));

            TRELLIS_LOG_ACTION("conn", connection_id, "Sending CONNECT_OK.");
            send_raw(buffer, size);

            handshake = handshake_state{
                asio::steady_timer(context->get_io()),
                buffer,
            };
        }

        // Should always be in PENDING state because the handshake is cancelled when we receive CONNECT_OK.
        assert(state == connection_state::PENDING);

        // 200ms for now, should probably be dynamic in the future.
        auto cancelled = handshake->timer.expires_from_now(std::chrono::milliseconds{200});

        // If we cancelled a pending operation, we should probably resend immediately instead of waiting.
        if (cancelled == 1) {
            handshake->timer.expires_from_now(std::chrono::milliseconds{0});
        }

        handshake->timer.async_wait([this](asio::error_code ec) {
            if (ec == asio::error::operation_aborted) {
                return;
            }

            // The only possible error is operation_aborted.
            assert(!ec);

            // Should be in the CONNECTING state because if we received a CONNECT_OK then this timer should have been cancelled.
            assert(state == connection_state::PENDING);

            constexpr auto size = sizeof(headers::type) + sizeof(headers::connect_ok);

            TRELLIS_LOG_ACTION("conn", connection_id, "Resending CONNECT_OK due to timeout.");

            send_raw(handshake->buffer, size);

            send_connect_ok();
        });
    }

    /**
     * Final phase of handshake, server side.
     * Marks the connection as ESTABLISHED if it isn't already, and calls the on_establish callback.
     * The callback isn't stored, feel free to capture locals by reference.
     */
    template <typename F>
    void receive_connect_ack(const F& on_establish) {
        if (state == connection_state::PENDING) {
            TRELLIS_LOG_ACTION("conn", connection_id, "Received CONNECT_ACK. Now ESTABLISHED.");

            cancel_handshake();

            state = connection_state::ESTABLISHED;
            on_establish();
        } else {
            TRELLIS_LOG_ACTION("conn", connection_id, "Received CONNECT_ACK on non-PENDING connection. Ignoring.");
        }
    }

    void cancel_handshake() {
        TRELLIS_LOG_ACTION("conn", connection_id, "Cancelling handshake.");

        // Can't cancel something that doesn't exist.
        assert(handshake);

        handshake = std::nullopt;
    }

    /** Disconnects without sending DISCONNECT to the peer. Peer will be forced to timeout. */
    void disconnect_without_send() {
        if (state == connection_state::DISCONNECTED) {
            TRELLIS_LOG_ACTION("conn", connection_id, "Attempted to disconnect_without_send an already DISCONNECTED connection.");
            return;
        }

        TRELLIS_LOG_ACTION("conn", connection_id, "Disconnecting without sending DISCONNECT. Killing immediately.");

        state = connection_state::DISCONNECTED;

        context->kill(*this);
    }

    /** Sends a DATA_ACK. */
    void send_ack(std::uint8_t cid, config::sequence_id_t sid, config::sequence_id_t eid, config::fragment_id_t fid) {
        if (state == connection_state::DISCONNECTED) return;

        TRELLIS_LOG_ACTION("conn", connection_id, "Sending DATA_ACK (cid:", cid, ",sid:", sid, ",fid:", fid, ").");

        auto buffer = context->make_pending_buffer();
        auto type = headers::type::DATA_ACK;
        auto header = headers::data_ack{sid, eid, cid, fid};
        constexpr auto size = sizeof(type) + sizeof(header);

        std::memcpy(buffer.data(), &type, sizeof(type));
        std::memcpy(buffer.data() + sizeof(type), &header, sizeof(header));

        send_raw(buffer, size);
    }


private:
    struct handshake_state {
        timer_type timer;
        shared_datagram_buffer buffer;
    };

    context_base* context;
    protocol::endpoint client_endpoint;
    connection_state state;
    std::uint16_t connection_id;
    std::optional<handshake_state> handshake;
};

} // namespace trellis
