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
    using connection_ptr = std::shared_ptr<connection_type>;
    using protocol = asio::ip::udp;
    using receive_function = std::function<void(derived_type&, const connection_ptr&, std::istream&)>;

    friend connection_type;

    base_context(asio::io_context& io) :
        io(&io),
        socket(io),
        cache(),
        rng(std::random_device{}()),
        receive_funcs(),
        sender_endpoint(),
        buffer(),
        context_id(std::uniform_int_distribution<std::uint16_t>{}(rng)),
        running(false) {}

    auto get_io() -> asio::io_context& {
        return *io;
    }

    auto get_context_id() const -> std::uint16_t {
        return context_id;
    }

    auto make_pending_buffer() -> shared_datagram_buffer {
        return cache.make_pending_buffer();
    }

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

    auto get_rng() -> std::mt19937& {
        return rng;
    }

protected:
    auto get_socket() -> protocol::socket& {
        return socket;
    }

    auto get_socket() const -> const protocol::socket& {
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
            func = [](derived_type&, const connection_ptr&, std::istream&) {};
        }

        return func;
    }

private:
    void receive() {
        socket.async_receive_from(asio::buffer(buffer.data), sender_endpoint, [this](asio::error_code ec, std::size_t size) {
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

    asio::io_context* io;
    protocol::socket socket;
    datagram_buffer_cache cache;
    std::mt19937 rng;

    std::array<receive_function, traits::channel_count> receive_funcs;

    protocol::endpoint sender_endpoint;
    datagram_buffer buffer;

    std::uint16_t context_id;

    bool running;
};

} // namespace trellis
