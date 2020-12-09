#pragma once

#include "config.hpp"
#include "message_header.hpp"

#include <cassert>
#include <cstdint>
#include <algorithm>
#include <bitset>
#include <memory>
#include <optional>

namespace trellis {

class fragment_assembler {
public:
    static constexpr std::size_t fragment_size = config::datagram_size - headers::data_offset;

    fragment_assembler() :
        sequence_id{std::nullopt},
        buffer{},
        buffer_fragments{0},
        buffer_capacity{0},
        complete{} {}

    fragment_assembler(config::sequence_id_t sid, config::fragment_id_t num_fragments) :
        sequence_id(sid),
        buffer(std::make_unique<char[]>(num_fragments * fragment_size)),
        buffer_fragments(num_fragments),
        buffer_capacity(num_fragments * fragment_size),
        complete{} {
            assert(sequence_id);
            assert(num_fragments >= 2);
            assert(buffer);
            assert(complete.count() == 0);
        }

    auto get_sequence_id() const -> const std::optional<config::sequence_id_t>& {
        return sequence_id;
    }

    void reset(config::sequence_id_t sid, config::fragment_id_t num_fragments) {
        assert(num_fragments >= 2);

        auto required_size = num_fragments * fragment_size;

        if (required_size > buffer_capacity || buffer_capacity > required_size * 2) {
            buffer = std::make_unique<char[]>(required_size);
            buffer_capacity = required_size;
        }

        sequence_id = sid;
        buffer_fragments = num_fragments;
        complete = {};

        assert(sequence_id);
        assert(buffer);
        assert(buffer_fragments * fragment_size == required_size);
        assert(buffer_fragments * fragment_size <= buffer_capacity);
        assert(complete.count() == 0);
    }

    void receive(const headers::data& header, const char* b, const char* e) {
        assert(b);
        assert(e);
        assert(b < e);
        assert(std::size_t(e - b) <= fragment_size);
        assert(buffer);
        assert(header.fragment_count == buffer_fragments);
        assert(header.fragment_id < buffer_fragments);
        assert(!complete.test(header.fragment_id));
        assert(fragment_size * header.fragment_id + (e - b) < buffer_capacity);

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
        assert(complete.count() <= buffer_fragments);
        return complete.count() == buffer_fragments;
    }

    bool has_fragment(config::fragment_id_t id) const {
        assert(id < buffer_fragments);
        return complete.test(id);
    }

private:
    std::optional<config::sequence_id_t> sequence_id;
    std::unique_ptr<char[]> buffer;
    config::fragment_id_t buffer_fragments;
    std::size_t buffer_capacity;
    std::bitset<config::max_fragments> complete;
};

} // namespace trellis
