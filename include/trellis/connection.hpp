#pragma once

#include "context_base.hpp"
#include "connection_base.hpp"
#include "channel.hpp"
#include "context_traits.hpp"
#include "logging.hpp"
#include "message_header.hpp"
#include "utility.hpp"
#include "streams.hpp"

#include <asio.hpp>

#include <type_traits>
#include <random>
#include <optional>

namespace trellis {

/** Context-specific connection type. Handles sending and receiving data packets. */
template <typename Context>
class connection final : public connection_base {
public:
    friend Context;
    friend packetbuf<connection>;

    using context_type = Context;
    using traits = context_traits<context_type>;
    using channel_state_tuple = typename traits::template channel_tuple<channel, connection>;

    connection(context_type& context, const protocol::endpoint& client_endpoint) :
        connection_base(context, client_endpoint),
        channels(tuple_constructor<channel_state_tuple>{}(*this)) {
            TRELLIS_LOG_ACTION("conn", get_connection_id(), "Connection constructed.");
        }

    connection(const connection&) = delete;
    connection(connection&&) = delete;

    auto get_stats() const -> std::array<connection_stats, traits::channel_count> {
        return get_stats(std::make_index_sequence<traits::channel_count>{});
    }

    template <typename Channel, typename F>
    void send(F&& func) {
        auto stream = opacketstream<Channel, connection>(*this);
        std::forward<F>(func)(stream);
    }

private:
    // Exposed as provate members so Context can access them.
    using connection_base::send_raw;
    using connection_base::send_connect;
    using connection_base::receive_connect_ok;
    using connection_base::send_connect_ok;
    using connection_base::receive_connect_ack;
    using connection_base::cancel_handshake;
    using connection_base::disconnect_without_send;
    using connection_base::send_ack;

    /** Sends all data packets in the given iterator range. Generates data headers and writes them to the front of the buffers. */
    template <typename Channel, typename Iter>
    void send_data(Iter b, Iter e, std::size_t last_payload_size) {
        constexpr auto channel_index = traits::template channel_index<Channel>;

        // last_payload_size is a calculated value, so double-check it here.
        assert(last_payload_size <= config::datagram_size - headers::data_offset);

        auto& channel = std::get<channel_index>(channels);
        auto type = headers::type::DATA;
        auto num_fragments = e - b;
        auto sid = channel.next_sequence_id();

        // It's not our responsibility to ensure that the fragments are within the limit. The caller should check this.
        assert(num_fragments <= std::numeric_limits<config::fragment_id_t>::max());

        TRELLIS_LOG_ACTION("conn", get_connection_id(), "Sending data (sid:", sid, ",fragments:", num_fragments, ",lps:", last_payload_size, ")");

        for (auto iter = b; iter != e; ++iter) {
            auto& buffer = *iter;

            auto header = headers::data{};
            header.sequence_id = sid;
            header.channel_id = channel_index;
            header.fragment_count = num_fragments;
            header.fragment_id = iter - b;

            std::memcpy(buffer.data(), &type, sizeof(type));
            std::memcpy(buffer.data() + sizeof(type), &header, sizeof(headers::data));

            if (iter == e - 1) {
                channel.send_packet(header, buffer, last_payload_size + headers::data_offset);
            } else {
                channel.send_packet(header, buffer, config::datagram_size);
            }
        }
    }

    /**
     * Receives a DATA datagram, and if it completes the message, calls data_handler with the results.
     * If the connection is still PENDING, becomes ESTABLISHED and calls on_establish.
     * Neither of the callbacks are stored, feel free to capture locals by reference.
     */
    template <typename F, typename G>
    void receive(const headers::data& header, const datagram_buffer& datagram, std::size_t count, const F& data_handler, const G& on_establish) {
        receive(header, datagram, count, data_handler, on_establish, std::make_index_sequence<std::tuple_size_v<channel_state_tuple>>{});
    }

    /** Do not use. Call the other overload instead. */
    template <typename F, typename G, std::size_t... Is>
    void receive(const headers::data& header, const datagram_buffer& datagram, std::size_t count, const F& data_handler, const G& on_establish, std::index_sequence<Is...>) {
        // should only be called from the context's receive handler, so we should be in the networking thread
        assert(get_context().is_thread_current());

        // If still pending, go ahead and establish. We haven't received a CONNECT_ACK yet, but we'll still allow the client to start sending DATA.
        if (get_state() == connection_state::PENDING) {
            TRELLIS_LOG_ACTION("conn", get_connection_id(), "Received DATA while PENDING. Now ESTABLISHED.");

            cancel_handshake();

            set_state(connection_state::ESTABLISHED);
            on_establish();
        }

        // Only ESTABLISHED connections should receive DATA messages.
        assert(get_state() == connection_state::ESTABLISHED);

        ((Is == header.channel_id ? (std::get<Is>(channels).receive(header, datagram, count, data_handler), true) : false) || ...);
    }

    /**
     * Receives a DATA_ACK datagram.
     */
    void receive_ack(const headers::data_ack& header) {
        receive_ack(header, std::make_index_sequence<std::tuple_size_v<channel_state_tuple>>{});
    }

    /** Do not use. Call the other overload instead. */
    template <std::size_t... Is>
    void receive_ack(const headers::data_ack& header, std::index_sequence<Is...>) {
        // should only be called from the context's receive handler, so we should be in the networking thread
        assert(get_context().is_thread_current());

        // Only ESTABLISHED connections should receive DATA_ACK messages.
        assert(get_state() == connection_state::ESTABLISHED);

        ((Is == header.channel_id ? (std::get<Is>(channels).receive_ack(header), true) : false) || ...);
    }

    template <std::size_t... Is>
    auto get_stats(std::index_sequence<Is...>) const -> std::array<connection_stats, traits::channel_count> {
        return {std::get<Is>(channels).get_stats()...};
    }

    /** Channel states. */
    channel_state_tuple channels;
};

} // namespace trellis
