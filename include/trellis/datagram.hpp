#pragma once

#include "config.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <list>

namespace trellis {

using datagram_buffer = std::array<char, config::datagram_size>;

class datagram_buffer_cache {
public:
    using buffer_list = std::list<datagram_buffer>;
    using buffer_iterator = buffer_list::iterator;

    datagram_buffer_cache() :
        pending_buffers(),
        next_buffer(pending_buffers.end()) {}

    auto make_pending_buffer() -> buffer_iterator {
        if (next_buffer == pending_buffers.end()) {
            next_buffer = pending_buffers.emplace(pending_buffers.end());
        }

        auto iter = next_buffer;

        ++next_buffer;

        return iter;
    }

    void free_pending_buffer(buffer_iterator iter) {
        // If the list is empty, we can't possibly free anything.
        assert(next_buffer != pending_buffers.begin());

        // The iterator must be valid, otherwise this is probably a double-free.
        assert([&]{
            for (auto i = pending_buffers.begin(); i != next_buffer; ++i) {
                if (i == iter) return true;
            }
            return false;
        }());

        --next_buffer;
        if (iter != next_buffer) {
            std::swap(*iter, *next_buffer);
        }
    }

private:
    buffer_list pending_buffers;
    buffer_iterator next_buffer;
};

} // namespace trellis
