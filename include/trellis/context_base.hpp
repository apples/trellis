#pragma once

#include "config.hpp"
#include "datagram.hpp"
#include "streams_fwd.hpp"

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
    friend _detail::packetbuf_base;

    using protocol = asio::ip::udp;

    using executor_type = asio::strand<asio::io_context::executor_type>;

    /** Constructs a context for the given io_context. */
    context_base(asio::io_context& io) :
        io(&io),
        strand(asio::make_strand(io)),
        socket(io),
        cache(),
        rng(std::random_device{}()),
        context_id(std::uniform_int_distribution<std::uint16_t>{}(rng)) {}

    virtual ~context_base() = 0;

    /** Gets a reference to the io_context. */
    auto get_io() -> asio::io_context& {
        return *io;
    }

    /** Dispatches a task to the context's executor. */
    template <typename F>
    void dispatch(F&& f) {
        asio::dispatch(strand, std::forward<F>(f));
    }

    /** Binds the context's executor to the given task and returns the bound object. */
    template <typename F>
    auto bind_executor(F&& f) const {
        return asio::bind_executor(strand, std::forward<F>(f));
    }

    /** Gets the context's executor. */
    auto get_executor() -> const executor_type& {
        return strand;
    }

    /** Gets the context's ID. Random and not guaranteed to be unique. */
    auto get_context_id() const -> std::uint16_t {
        return context_id;
    }

    /** Gets the context's uniform random bit generator. */
    auto get_rng() -> std::mt19937& {
        return rng;
    }

    /** Determines if the context's executor is responsible for the current thread of execution. */
    auto is_thread_current() const -> bool {
        return strand.running_in_this_thread();
    }

protected:
    /** Makes a packet buffer using the underlying cache. */
    auto make_pending_buffer() -> _detail::shared_datagram_buffer {
        return cache.make_pending_buffer();
    }

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
    executor_type strand;
    protocol::socket socket;
    _detail::datagram_buffer_cache cache;
    std::mt19937 rng;
    std::uint16_t context_id;
};

inline context_base::~context_base() = default;

} // namespace trellis
