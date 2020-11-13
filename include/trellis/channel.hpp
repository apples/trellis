#pragma once

#include "config.hpp"
#include "datagram.hpp"
#include "message_header.hpp"
#include "streams.hpp"

#include <bitset>
#include <limits>
#include <memory>

namespace trellis {

template <typename T, bool R, bool O>
struct channel_def {
    using tag_t = T;
    static constexpr bool is_reliable = R;
    static constexpr bool is_ordered = O;
};

class fragment_assembler {
public:
    static constexpr std::size_t fragment_size = config::datagram_size - headers::data_offset;

    int get_sequence_id() const {
        return sequence_id;
    }

    void reset(int sid, int num_fragments) {
        assert(sid != -1);
        assert(num_fragments >= 2);

        auto required_size = num_fragments * fragment_size;

        if (required_size > buffer_capacity || buffer_capacity > required_size * 2) {
            buffer = std::make_unique<char[]>(required_size);
            buffer_capacity = required_size;
        }

        sequence_id = sid;
        buffer_fragments = num_fragments;
        complete = {};

        assert(buffer);
        assert(buffer_fragments * fragment_size == required_size);
        assert(buffer_fragments * fragment_size <= buffer_capacity);
        assert(complete.count() == 0);
    }

    void receive(const headers::data& header, const char* b, const char* e) {
        assert(b);
        assert(e);
        assert(b < e);
        assert(b - e <= fragment_size);
        assert(buffer);
        assert(header.fragment_count == buffer_fragments);
        assert(header.fragment_id < buffer_fragments);
        assert(!complete.test(header.fragment_id));

        std::copy(b, e, buffer.get() + fragment_size * header.fragment_id);
        complete.set(header.fragment_id);
    }

    char* data() {
        assert(is_complete());

        return buffer.get();
    }

    std::size_t size() const {
        return buffer_fragments * fragment_size;
    }

    bool is_complete() const {
        return complete.count() == buffer_fragments;
    }

private:
    int sequence_id = -1;
    std::unique_ptr<char[]> buffer;
    std::size_t buffer_fragments = 0;
    std::size_t buffer_capacity = 0;
    std::bitset<config::max_fragments> complete = {};
};

template <typename Channel>
struct channel;

template <typename Tag>
class channel<channel_def<Tag, false, false>> {
public:
    channel() :
        sequence_id(0x9384), // TODO: randomize
        assemblers() {}

    auto next_sequence_id() -> std::uint16_t {
        return sequence_id++;
    }

    template <typename F>
    void receive(const headers::data& header, const datagram_buffer& datagram, size_t count, const F& on_receive_func) {
        assert(count <= config::datagram_size);
        assert(count >= headers::data_offset);

        if (header.fragment_count == 1) {
            assert(header.fragment_id == 0);
            auto b = datagram.data() + headers::data_offset;
            auto e = datagram.data() + count;
            assert(e <= datagram.data() + datagram.size());
            auto istream = ibytestream(b, e);
            on_receive_func(istream);
        } else {
            assert(header.fragment_id < header.fragment_count);
            auto slot = header.sequence_id % config::assembler_slots;
            auto& assembler = assemblers[slot];
            auto current_sid = assembler.get_sequence_id();

            using limits = std::numeric_limits<decltype(headers::data::sequence_id)>;
            auto max_gap = limits::max() / 2 - limits::min() / 2;

            auto is_stale = current_sid == -1 ||
                            current_sid < header.sequence_id && header.sequence_id - current_sid <= max_gap ||
                            current_sid > header.sequence_id && current_sid - header.sequence_id > max_gap;

            if (is_stale) {
                assembler.reset(header.sequence_id, header.fragment_count);
            }

            if (current_sid == header.sequence_id) {
                auto b = datagram.data() + headers::data_offset;
                auto e = datagram.data() + count;
                assert(e <= datagram.data() + datagram.size());
                assembler.receive(header, b, e);

                if (assembler.is_complete()) {
                    auto istream = ibytestream(assembler.data(), assembler.data() + assembler.size());
                    on_receive_func(istream);
                }
            }
        }
    }

private:
    std::uint16_t sequence_id;
    std::array<fragment_assembler, config::assembler_slots> assemblers;
};

} // namespace trellis
