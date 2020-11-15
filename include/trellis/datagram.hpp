#pragma once

#include "config.hpp"

#include <asio.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <list>
#include <utility>

namespace trellis {

struct datagram_buffer {
    std::array<char, config::datagram_size> data;
    int refcount = 0;
};

using datagram_buffer_list = std::list<datagram_buffer>;
using datagram_buffer_iterator = datagram_buffer_list::iterator;

class datagram_buffer_cache;

class shared_datagram_buffer {
public:
    friend class datagram_buffer_cache;

    shared_datagram_buffer();

    shared_datagram_buffer(shared_datagram_buffer&& other);

    shared_datagram_buffer(const shared_datagram_buffer& other);

    shared_datagram_buffer& operator=(shared_datagram_buffer&& other);

    shared_datagram_buffer& operator=(const shared_datagram_buffer& other);

    ~shared_datagram_buffer();

    auto buffer(std::size_t size = config::datagram_size) -> asio::mutable_buffer;

    auto buffer(std::size_t size = config::datagram_size) const -> asio::const_buffer;

    auto operator[](std::size_t i) -> char&;

    auto operator[](std::size_t i) const -> const char&;

    auto data() -> char*;

    auto data() const -> const char*;

    auto size() const -> std::size_t;

    void clear();

private:
    shared_datagram_buffer(datagram_buffer_cache& cache, datagram_buffer_iterator iter);

    datagram_buffer_cache* cache;
    datagram_buffer_iterator iter;
};

class datagram_buffer_cache {
public:
    friend class shared_datagram_buffer;

    datagram_buffer_cache() :
        pending_buffers(),
        free_buffers() {}

    auto make_pending_buffer() -> shared_datagram_buffer {
        if (free_buffers.empty()) {
            free_buffers.emplace(free_buffers.end());
        }

        assert(!free_buffers.empty());

        auto iter = free_buffers.begin();

        pending_buffers.splice(pending_buffers.end(), free_buffers, iter);

        return {*this, iter};
    }

private:
    void free_pending_buffer(datagram_buffer_iterator iter) {
        // If the list is empty, we can't possibly free anything.
        assert(!pending_buffers.empty());

        // The iterator must be valid, otherwise this is probably a double-free.
        assert([&]{
            for (auto i = pending_buffers.begin(); i != pending_buffers.end(); ++i) {
                if (i == iter) return true;
            }
            return false;
        }());

        // Buffers being freed should always have zero refcount.
        assert(iter->refcount == 0);

        free_buffers.splice(free_buffers.end(), pending_buffers, iter);
    }

    datagram_buffer_list pending_buffers;
    datagram_buffer_list free_buffers;
};


inline shared_datagram_buffer::shared_datagram_buffer() :
    cache(nullptr),
    iter() {}

inline shared_datagram_buffer::shared_datagram_buffer(shared_datagram_buffer&& other) :
    cache(std::exchange(other.cache, nullptr)),
    iter(std::move(other.iter)) {
        assert(!cache || iter->refcount > 0);
        assert(!other.cache);
    }

inline shared_datagram_buffer::shared_datagram_buffer(const shared_datagram_buffer& other) :
    cache(other.cache),
    iter(other.iter) {
        if (cache) {
            assert(iter->refcount > 0);
            ++(iter->refcount);
        }
    }

inline shared_datagram_buffer& shared_datagram_buffer::operator=(shared_datagram_buffer&& other) {
    assert(this != &other);
    assert(!cache || iter->refcount > 0);
    if (cache && --(iter->refcount) == 0) {
        cache->free_pending_buffer(iter);
    }
    cache = std::exchange(other.cache, nullptr);
    iter = std::move(other.iter);
    return *this;
}

inline shared_datagram_buffer& shared_datagram_buffer::operator=(const shared_datagram_buffer& other) {
    if (this == &other) return *this;
    assert(!cache || iter->refcount > 0);
    if (cache && --(iter->refcount) == 0) {
        cache->free_pending_buffer(iter);
    }
    cache = other.cache;
    iter = other.iter;
    if (cache) {
        assert(iter->refcount > 0);
        ++(iter->refcount);
    }
    assert(!cache || iter->refcount > 0);
    return *this;
}

inline shared_datagram_buffer::~shared_datagram_buffer() {
    assert(!cache || iter->refcount > 0);
    if (cache && --(iter->refcount) == 0) {
        cache->free_pending_buffer(iter);
    }
}

inline auto shared_datagram_buffer::buffer(std::size_t size) -> asio::mutable_buffer {
    assert(cache && iter->refcount > 0);
    return asio::buffer(iter->data, size);
}

inline auto shared_datagram_buffer::buffer(std::size_t size) const -> asio::const_buffer {
    assert(cache && iter->refcount > 0);
    return asio::buffer(iter->data, size);
}

inline auto shared_datagram_buffer::operator[](std::size_t i) -> char& {
    assert(cache && iter->refcount > 0);
    return iter->data[i];
}

inline auto shared_datagram_buffer::operator[](std::size_t i) const -> const char& {
    assert(cache && iter->refcount > 0);
    return iter->data[i];
}

inline auto shared_datagram_buffer::data() -> char* {
    assert(cache && iter->refcount > 0);
    return iter->data.data();
}

inline auto shared_datagram_buffer::data() const -> const char* {
    assert(cache && iter->refcount > 0);
    return iter->data.data();
}

inline auto shared_datagram_buffer::size() const -> std::size_t {
    assert(cache && iter->refcount > 0);
    return iter->data.size();
}

inline void shared_datagram_buffer::clear() {
    assert(cache && iter->refcount > 0);
    iter->data.fill('\0');
}

inline shared_datagram_buffer::shared_datagram_buffer(datagram_buffer_cache& cache, datagram_buffer_iterator iter) :
    cache(&cache),
    iter(iter) {
        assert(iter->refcount == 0);
        ++(iter->refcount);
    }

} // namespace trellis
