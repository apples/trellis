#pragma once

#include "context_base.hpp"
#include "config.hpp"
#include "connection.hpp"
#include "datagram.hpp"
#include "logging.hpp"
#include "message_header.hpp"
#include "lock_free_queue.hpp"
#include "event.hpp"
#include "utility.hpp"
#include "streams_fwd.hpp"

#include <asio.hpp>

#include <cstring>
#include <cassert>
#include <array>
#include <iostream>
#include <tuple>
#include <memory>

namespace trellis {

/** Implements behavior common to all contexts. */
template <typename C>
class context_crtp : public context_base {
public:
    using derived_type = C;
    using traits = context_traits<derived_type>;
    using connection_type = connection<derived_type>;
    using connection_ptr = std::shared_ptr<connection_type>;
    using receive_function = std::function<void(derived_type&, const connection_ptr&, std::istream&)>;

    friend connection_type;

    /** Constructs a context running on the given io_context. */
    context_crtp(asio::io_context& io) :
        context_base(io),
        sender_endpoint(),
        buffer(),
        running(false),
        events() {}

    /** Closes all connections and stops the context. */
    void stop() {
        if (running) {
            running = false;
            auto derived = static_cast<derived_type*>(this);
            derived->disconnect_all();
        }
    }

    /** Determines whether the context is currently running. */
    bool is_running() const {
        return running;
    }

    /**
     * Processes queued events and submits them to the given Handler.
     * 
     * Data messages are automatically routed to the overload matching the channel's tagged type.
     * 
     * Handler concept:
     * ```
     * template<typename T>
     * concept Handler = requires(T a) {
     *     a.on_connect(connection_ptr{});
     *     a.on_disconnect(connection_ptr{}, asio::error_code{});
     *     a.on_receive(Channels{}, connection_ptr{}, std::decltype<std::istream&>())...;
     * };
     * ```
     */
    template <typename Handler>
    void poll_events(Handler&& handler) {
        // must be executed from user thread
        assert(!is_thread_current());

        while (auto e = events.pop()) {
            TRELLIS_LOG_ACTION("context_crtp", this->get_context_id(), "Dispatching event (type:", e->index(), ")");

            std::visit(_detail::overload {
                [&](const _detail::event_connect& e) {
                    handler.on_connect(std::static_pointer_cast<connection_type>(e.conn));
                },
                [&](const _detail::event_disconnect& e) {
                    handler.on_disconnect(std::static_pointer_cast<connection_type>(e.conn), e.ec);
                },
                [&](const _detail::event_receive& e) {
                    auto istream = _detail::ibytestream(e.data.data.get(), e.data.data_len);
                    traits::with_channel_type(e.channel_id, [&](auto channel_type) {
                        handler.on_receive(channel_type, std::static_pointer_cast<connection_type>(e.conn), istream);
                    });
                },
            }, *e);
        }
    }

protected:
    using context_base::make_pending_buffer;

    void open(const protocol::endpoint& endpoint) {
        // must be executed from user thread
        assert(!is_thread_current());

        get_socket().open(endpoint.protocol());
        get_socket().set_option(asio::ip::v6_only{false});
        get_socket().bind(endpoint);
        running = true;
        receive();
    }

    void close() {
        running = false;
        get_socket().shutdown(asio::socket_base::shutdown_both);
        get_socket().close();
    }

    void push_event(_detail::event&& e) {
        // must be executed from networking thread
        assert(is_thread_current());

        events.push(std::move(e));
    }

private:
    void receive() {
        sender_endpoint = {};
        get_socket().async_receive_from(asio::buffer(buffer.data), sender_endpoint, this->bind_executor([this](asio::error_code ec, std::size_t size) {
            if (!ec) {
                if (running) {
                    TRELLIS_LOG_DATAGRAM("recv", buffer.data, size);
                    auto derived = static_cast<derived_type*>(this);
                    derived->receive(buffer, sender_endpoint, size);
                    if (running) {
                        receive();
                    }
                }
            } else {
                switch (ec.value()) {
                    case asio::error::operation_aborted:
                        return;
                    default: {
                        TRELLIS_LOG_ACTION("receive", sender_endpoint, ec.category().name(), "(", ec.value(), "): ", ec.message());
                        if (sender_endpoint != protocol::endpoint{}) {
                            auto derived = static_cast<derived_type*>(this);
                            derived->connection_error(sender_endpoint, ec);
                        }
                        if (running) {
                            receive();
                        }
                        return;
                    }
                }
            }
        }));
    }

    protocol::endpoint sender_endpoint;
    _detail::datagram_buffer buffer;
    std::atomic<bool> running;
    _detail::lock_free_queue<_detail::event> events;
};

} // namespace trellis
