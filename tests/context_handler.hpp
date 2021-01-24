#pragma once

#include <asio.hpp>

#include <utility>

template <typename Context, typename Channel, typename OnReceive>
class channel_handler {
public:
    using context_type = Context;
    using connection_ptr = typename context_type::connection_ptr;
    using channel_type = Channel;
    using on_receive_type = OnReceive;

    explicit channel_handler(on_receive_type on_receive) :
        on_receive_func(std::move(on_receive)) {}

    void on_receive(channel_type, const connection_ptr& conn, std::istream& istream) {
        on_receive_func(channel_type{}, conn, istream);
    }

private:
    on_receive_type on_receive_func;
};

template <typename Context, typename OnConnect, typename OnDisconnect, typename... OnReceives>
class context_handler;

template <typename... Channels, template <typename...> typename Context, typename OnConnect, typename OnDisconnect, typename... OnReceives>
class context_handler<Context<Channels...>, OnConnect, OnDisconnect, OnReceives...> : public channel_handler<Context<Channels...>, Channels, OnReceives>... {
public:
    using context_type = Context<Channels...>;
    using connection_ptr = typename context_type::connection_ptr;
    using on_connect_type = OnConnect;
    using on_disconnect_type = OnDisconnect;

    using channel_handler<context_type, Channels, OnReceives>::on_receive...;

    context_handler(context_type& context, on_connect_type on_connect, on_disconnect_type on_disconnect, OnReceives... on_receives) :
        channel_handler<context_type, Channels, OnReceives>(std::move(on_receives))...,
        context(&context),
        timer(context.get_io()),
        on_connect_func(std::move(on_connect)),
        on_disconnect_func(std::move(on_disconnect)) {}
    
    void on_connect(const connection_ptr& conn) {
        on_connect_func(conn);
    }

    void on_disconnect(const connection_ptr& conn, asio::error_code ec) {
        on_disconnect_func(conn, ec);
    }

    void poll() {
        timer.expires_from_now(std::chrono::milliseconds{10});
        timer.async_wait([&](asio::error_code ec) {
            if (ec || !context->is_running()) return;
            context->poll_events(*this);
            poll();
        });
    }

private:
    context_type* context;
    asio::steady_timer timer;
    on_connect_type on_connect_func;
    on_disconnect_type on_disconnect_func;
};

template <typename Context, typename OnConnect, typename OnDisconnect, typename... OnReceives>
context_handler(Context&, OnConnect, OnDisconnect, OnReceives...) -> context_handler<Context, OnConnect, OnDisconnect, OnReceives...>;
