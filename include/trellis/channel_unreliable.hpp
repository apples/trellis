#pragma once

#include "config.hpp"
#include "datagram.hpp"
#include "fragment_assembler.hpp"
#include "message_header.hpp"
#include "logging.hpp"
#include "streams.hpp"
#include "connection_stats.hpp"
#include "raw_buffer.hpp"

#include <string_view>

namespace trellis {

/** Channel implementing an unreliable protocol. */
class channel_unreliable {
public:
    channel_unreliable(connection_base& conn) :
        conn(&conn),
        sequence_id(0),
        assemblers() {}

    channel_unreliable(const channel_unreliable&) = delete;
    channel_unreliable(channel_unreliable&&) = delete;

    auto next_sequence_id() -> config::sequence_id_t {
        return sequence_id++;
    }

    void send_packet([[maybe_unused]] const headers::data& header, const shared_datagram_buffer& datagram, std::size_t size) {
        conn->send_raw(datagram, size);
    }

    void receive_ack(const headers::data_ack& header) {
        // should only be called from the connections's receive handler, so we should be in the networking thread
        assert(conn->get_context().is_thread_current());

        TRELLIS_LOG_ACTION("channel", +header.channel_id, "Received unexpected DATA_ACK (sid:", header.sequence_id, ",fid:", header.fragment_id, "). Disconnecting.");
        conn->disconnect();
    }

    auto get_stats() const -> connection_stats {
        return {
            0,
            int(assemblers.size()),
        };
    }

protected:
    auto receive_impl(const headers::data& header, const datagram_buffer& datagram, size_t count) -> std::optional<raw_buffer> {
        // should only be called from the connections's receive handler, so we should be in the networking thread
        assert(conn->get_context().is_thread_current());

        assert(count <= config::datagram_size);
        assert(count >= headers::data_offset);

        if (header.fragment_count == 1) {
            // Shortcut for non-fragmented packets

            TRELLIS_LOG_ACTION("channel", +header.channel_id, "Processing message ", header.sequence_id, " as non-fragmented.");

            assert(header.fragment_id == 0);

            auto b = datagram.data.data() + headers::data_offset;
            auto e = datagram.data.data() + count;

            assert(count <= config::datagram_size);

            auto data = std::make_unique<char[]>(e - b);

            std::copy(b, e, data.get());

            return raw_buffer{std::move(data), static_cast<std::size_t>(e - b)};
        } else {
            TRELLIS_LOG_ACTION("channel", +header.channel_id, "Processing message ", header.sequence_id, " as fragment piece ", +header.fragment_id, " / ", +header.fragment_count, ".");

            assert(header.fragment_id < header.fragment_count);
            auto slot = header.sequence_id % config::assembler_slots;
            auto& assembler = assemblers[slot];

            auto is_stale = [&]() -> bool {
                auto& current_sid = assembler.get_sequence_id();
                return !current_sid || sequence_id_less(*current_sid, header.sequence_id);
            }();

            if (is_stale) {
                TRELLIS_LOG_ACTION("channel", +header.channel_id, "Resetting assembler in slot ", slot, ".");

                assembler.reset(header.sequence_id, header.fragment_count);
            }

            if (assembler.get_sequence_id() == header.sequence_id) {
                TRELLIS_LOG_ACTION("channel", +header.channel_id, "Handing packet to assembler in slot ", slot, ".");

                auto b = datagram.data.data() + headers::data_offset;
                auto e = datagram.data.data() + count;
                assert(count <= config::datagram_size);
                assembler.receive(header, b, e);

                if (assembler.is_complete()) {
                    TRELLIS_LOG_ACTION("channel", +header.channel_id, "Message reassembly is complete.");

                    return raw_buffer{assembler.release(), assembler.size()};
                } else {
                    return std::nullopt;
                }
            } else {
                return std::nullopt;
            }
        }
    }

    connection_base* conn;
    std::atomic<config::sequence_id_t> sequence_id;
    std::array<fragment_assembler, config::assembler_slots> assemblers;
};

} // namespace trellis
