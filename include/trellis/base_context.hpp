#pragma once

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

template <typename C>
class base_context {
public:
    using derived_type = C;
    using traits = context_traits<derived_type>;
    using connection_type = connection<derived_type>;
    using protocol = asio::ip::udp;
    using buffer_iterator = datagram_buffer_cache::buffer_iterator;
    using receive_function = std::function<void(derived_type&, const std::shared_ptr<connection_type>&, std::istream&)>;

    base_context(asio::io_context& io) :
        socket(io),
        cache(),
        receive_funcs(),
        sender_endpoint(),
        buffer(),
        running(false) {}

    auto make_pending_buffer() -> buffer_iterator {
        return cache.make_pending_buffer();
    }

    void free_pending_buffer(buffer_iterator iter) {
        cache.free_pending_buffer(iter);
    }

    void stop() {
        running = false;
        auto derived = static_cast<derived_type*>(this);
        derived->disconnect_all();
    }

    template <typename Channel>
    void on_receive(receive_function func) {
        receive_funcs[traits::template channel_index<Channel>] = std::move(func);
    }

protected:
    auto get_socket() -> protocol::socket& {
        return socket;
    }

    void open(const protocol::endpoint& endpoint) {
        socket.open(endpoint.protocol());
        socket.bind(endpoint);
        running = true;
        receive();
    }

    void close() {
        running = false;
        socket.shutdown(asio::socket_base::shutdown_both);
        socket.close();
    }

    auto get_receive_func(int channel_id) -> const receive_function& {
        assert(channel_id >= 0);
        assert(channel_id < receive_funcs.size());

        auto& func = receive_funcs[channel_id];

        if (!func) {
            std::cerr << "Warning: No receive handler for channel_id " << channel_id << std::endl;
            func = [](derived_type&, const std::shared_ptr<connection_type>&, std::istream&) {};
        }

        return func;
    }

private:
    void receive() {
        socket.async_receive_from(asio::buffer(buffer), sender_endpoint, [this](asio::error_code ec, std::size_t size) {
            if (!ec && running) {
                TRELLIS_LOG_DATAGRAM("recv", buffer, size);
                auto derived = static_cast<derived_type*>(this);
                derived->receive(buffer, sender_endpoint, size);
                if (running) {
                    receive();
                }
            } else if (ec.value() != asio::error::operation_aborted) {
                std::cerr << "[trellis] ERROR " << ec.category().name() << ": " << ec.message() << std::endl;
                stop();
            }
        });
    }

    protocol::socket socket;
    datagram_buffer_cache cache;

    std::array<receive_function, traits::channel_count> receive_funcs;

    protocol::endpoint sender_endpoint;
    datagram_buffer buffer;

    bool running;
};

} // namespace trellis
