#pragma once

#include "config.hpp"
#include "datagram.hpp"

#include <asio.hpp>

#include <cstdint>
#include <random>

namespace trellis {

class connection_base;

/** Base context class. Holds data relevant to all contexts. */
class context_base {
public:
    friend connection_base;

    using protocol = asio::ip::udp;

    context_base(asio::io_context& io) :
        io(&io),
        socket(io),
        cache(),
        rng(std::random_device{}()),
        context_id(std::uniform_int_distribution<std::uint16_t>{}(rng)) {}

    virtual ~context_base() = 0;

    auto get_io() -> asio::io_context& {
        return *io;
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

protected:
    auto get_socket() -> protocol::socket& {
        return socket;
    }

    auto get_socket() const -> const protocol::socket& {
        return socket;
    }

    /** Kills and removes the given connection without sending a DISCONNECT. */
    virtual void kill(const connection_base& conn) = 0;

    virtual void connection_error(const connection_base& conn, asio::error_code ec) = 0;

private:
    asio::io_context* io;
    protocol::socket socket;
    datagram_buffer_cache cache;
    std::mt19937 rng;
    std::uint16_t context_id;
};

inline context_base::~context_base() = default;

} // namespace trellis
