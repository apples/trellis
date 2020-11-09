#pragma once

#include "channel.hpp"
#include "context_traits.hpp"
#include "logging.hpp"
#include "message_header.hpp"

#include <asio.hpp>

#include <type_traits>

namespace trellis {

template <typename Context>
class connection : public std::enable_shared_from_this<connection<Context>> {
public:
    friend Context;

    using context_type = Context;
    using traits = context_traits<context_type>;
    using channel_state_tuple = typename traits::channel_state_tuple;
    using buffer_iterator = typename context_type::buffer_iterator;

    using std::enable_shared_from_this<connection>::shared_from_this;

    connection(context_type& context, asio::ip::udp::socket& socket, asio::ip::udp::endpoint client_endpoint) :
        context(&context),
        socket(&socket),
        client_endpoint(client_endpoint),
        connected(true),
        channels() {}
    
    auto get_context() -> context_type& {
        return *context;
    }

    auto get_endpoint() const -> const asio::ip::udp::endpoint& {
        return client_endpoint;
    }

    void send_connect() {
        auto buffer = context->make_pending_buffer();
        auto type = headers::type::CONNECT;
        std::memcpy(buffer->data(), &type, sizeof(type));
        send_raw(buffer, sizeof(type));
    }

    void send_connect_ok() {
        auto buffer = context->make_pending_buffer();
        auto type = headers::type::CONNECT_OK;
        std::memcpy(buffer->data(), &type, sizeof(type));
        send_raw(buffer, sizeof(type));
    }

    template <typename Channel, typename Iter>
    void send_data(Iter b, Iter e, std::size_t last_payload_size) {
        constexpr auto channel_index = traits::template channel_index<Channel>;

        assert(last_payload_size + headers::data_offset <= config::datagram_size);

        auto& channel = std::get<channel_index>(channels);
        auto type = headers::type::DATA;
        auto num_fragments = e - b;
        auto sid = channel.next_sequence_id();
        
        for (auto iter = b; iter != e; ++iter) {
            auto& buffer = *iter;

            auto header = headers::data{};
            header.sequence_id = sid;
            header.channel_id = channel_index;
            header.fragment_count = num_fragments;
            header.fragment_id = iter - b;

            std::memcpy(buffer->data(), &type, sizeof(type));
            std::memcpy(buffer->data() + sizeof(type), &header, sizeof(headers::data));

            if (iter == e - 1) {
                send_raw_nofree(buffer, last_payload_size + headers::data_offset);
            } else {
                send_raw_nofree(buffer, config::datagram_size);
            }
        }
    }

    void disconnect() {
        disconnect([]{});
    }

    template <typename F>
    void disconnect(const F& func) {
        if (!connected) return;

        connected = false;

        auto buffer = context->make_pending_buffer();
        auto type = headers::type::DISCONNECT;
        std::memcpy(buffer->data(), &type, sizeof(type));

        TRELLIS_LOG_DATAGRAM("d/cn", *buffer, sizeof(type));

        socket->async_send_to(asio::buffer(*buffer, sizeof(type)), client_endpoint, [buffer, func, ptr = this->shared_from_this()](asio::error_code ec, std::size_t size) {
            ptr->context->free_pending_buffer(buffer);
            ptr->context->kill(*ptr);
            func();
        });
    }

private:
    void send_raw(buffer_iterator data, std::size_t count) {
        if (!connected) return;

        TRELLIS_LOG_DATAGRAM("send", *data, count);

        socket->async_send_to(asio::buffer(*data, count), client_endpoint, [data, ptr = shared_from_this()](asio::error_code ec, std::size_t size) {
            ptr->context->free_pending_buffer(data);

            if (ec) {
                std::cerr << "Error: " << ec.category().name() << ": " << ec.message() << std::endl;
                ptr->disconnect();
            }
        });
    }

    void send_raw_nofree(buffer_iterator data, std::size_t count) {
        if (!connected) return;

        TRELLIS_LOG_DATAGRAM("send", *data, count);

        socket->async_send_to(asio::buffer(*data, count), client_endpoint, [ptr = shared_from_this()](asio::error_code ec, std::size_t size) {
            if (ec) {
                std::cerr << "Error: " << ec.category().name() << ": " << ec.message() << std::endl;
                ptr->disconnect();
            }
        });
    }

    template <typename F>
    void receive(const headers::data& header, const datagram_buffer& datagram, std::size_t count, const F& func) {
        receive(header, datagram, count, func, std::make_index_sequence<std::tuple_size_v<channel_state_tuple>>{});
    }

    template <typename F, std::size_t... Is>
    void receive(const headers::data& header, const datagram_buffer& datagram, std::size_t count, const F& func, std::index_sequence<Is...>) {
        ((Is == header.channel_id ? (std::get<Is>(channels).receive(header, datagram, count, func), true) : false) || ...);
    }

    context_type* context;
    asio::ip::udp::socket* socket;
    asio::ip::udp::endpoint client_endpoint;
    bool connected;
    channel_state_tuple channels;
};

} // namespace trellis
