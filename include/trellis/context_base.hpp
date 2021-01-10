#pragma once

#include "config.hpp"
#include "datagram.hpp"

#include <asio.hpp>

#include <cstdint>
#include <random>
#include <thread>

namespace trellis {

class connection_base;

/** Base context class. Holds data relevant to all contexts. */
class context_base {
public:
    friend connection_base;

    using protocol = asio::ip::udp;

    context_base(asio::io_context& io) :
        io(&io),
        strand(asio::make_strand(io)),
        socket(io),
        cache(),
        rng(std::random_device{}()),
        context_id(std::uniform_int_distribution<std::uint16_t>{}(rng)),
        thread_id() {}

    virtual ~context_base() = 0;

    auto get_io() -> asio::io_context& {
        return *io;
    }

    template <typename F>
    void dispatch(F&& f) {
        asio::dispatch(strand, std::forward<F>(f));
    }

    template <typename F>
    auto bind_executor(F&& f) {
        return asio::bind_executor(strand, std::forward<F>(f));
    }

    auto get_context_id() const -> std::uint16_t {
        return context_id;
    }

    auto make_pending_buffer() -> shared_datagram_buffer {
        return cache.make_pending_buffer();
    }

    auto get_rng() -> std::mt19937& {
        return rng;
    }

    auto is_thread_current() const -> bool {
        return strand.running_in_this_thread();
    }

protected:
    auto get_socket() -> protocol::socket& {
        return socket;
    }

    auto get_socket() const -> const protocol::socket& {
        return socket;
    }

    /** Kills and removes the given connection without sending a DISCONNECT. */
    virtual void kill(const connection_base& conn, const asio::error_code& ec) = 0;

    virtual void connection_error(const connection_base& conn, asio::error_code ec) = 0;

private:
    asio::io_context* io;
    asio::strand<asio::io_context::executor_type> strand;
    protocol::socket socket;
    datagram_buffer_cache cache;
    std::mt19937 rng;
    std::uint16_t context_id;
    std::thread::id thread_id;
};

inline context_base::~context_base() = default;

} // namespace trellis
