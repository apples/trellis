#pragma once

#include "context_base.hpp"
#include "config.hpp"
#include "connection.hpp"
#include "datagram.hpp"
#include "logging.hpp"
#include "message_header.hpp"

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

    context_crtp(asio::io_context& io) :
        context_base(io),
        receive_funcs(),
        sender_endpoint(),
        buffer(),
        running(false) {}

    void stop() {
        if (running) {
            running = false;
            auto derived = static_cast<derived_type*>(this);
            derived->disconnect_all();
        }
    }

    template <typename Channel>
    void on_receive(receive_function func) {
        receive_funcs[traits::template channel_index<Channel>] = std::move(func);
    }

protected:
    void open(const protocol::endpoint& endpoint) {
        get_socket().open(endpoint.protocol());
        get_socket().bind(endpoint);
        running = true;
        receive();
    }

    void close() {
        running = false;
        get_socket().shutdown(asio::socket_base::shutdown_both);
        get_socket().close();
    }

    auto get_receive_func(int channel_id) -> const receive_function& {
        assert(channel_id >= 0);
        assert(channel_id < int(receive_funcs.size()));

        auto& func = receive_funcs[channel_id];

        if (!func) {
            std::cerr << "Warning: No receive handler for channel_id " << channel_id << std::endl;
            func = [](derived_type&, const connection_ptr&, std::istream&) {};
        }

        return func;
    }

private:
    void receive() {
        get_socket().async_receive_from(asio::buffer(buffer.data), sender_endpoint, [this](asio::error_code ec, std::size_t size) {
            if (!ec) {
                if (running) {
                    TRELLIS_LOG_DATAGRAM("recv", buffer.data, size);
                    auto derived = static_cast<derived_type*>(this);
                    derived->receive(buffer, sender_endpoint, size);
                    if (running) {
                        receive();
                    }
                }
            } else if (ec.value() != asio::error::operation_aborted) {
                std::cerr << "[trellis] ERROR " << ec.category().name() << ": " << ec.message() << std::endl;
                stop();
            }
        });
    }

    std::array<receive_function, traits::channel_count> receive_funcs;
    protocol::endpoint sender_endpoint;
    datagram_buffer buffer;
    bool running;
};

} // namespace trellis
