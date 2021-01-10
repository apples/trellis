#pragma once

#include "config.hpp"

#include <asio.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <list>
#include <utility>

namespace trellis {

using datagram_storage = std::array<char, config::datagram_size>;

class datagram_buffer_cache;

/** Acts as a datagram_storage with a shared_ptr control block. */
struct datagram_buffer {
    datagram_storage data;
    std::atomic<int> refcount = 0;
    datagram_buffer* next = nullptr;
    datagram_buffer_cache* cache = nullptr;
};

/** Similar to a shared_ptr<datagram_storage>, but also provides direct access to the underlying storage. */
class shared_datagram_buffer {
public:
    friend class datagram_buffer_cache;

    shared_datagram_buffer();

    shared_datagram_buffer(shared_datagram_buffer&& other);

    shared_datagram_buffer(const shared_datagram_buffer& other);

    shared_datagram_buffer& operator=(shared_datagram_buffer&& other);

    shared_datagram_buffer& operator=(const shared_datagram_buffer& other);

    ~shared_datagram_buffer();

    explicit operator bool() const;

    auto buffer(std::size_t size = config::datagram_size) -> asio::mutable_buffer;

    auto buffer(std::size_t size = config::datagram_size) const -> asio::const_buffer;

    auto operator[](std::size_t i) -> char&;

    auto operator[](std::size_t i) const -> const char&;

    auto data() -> char*;

    auto data() const -> const char*;

    auto size() const -> std::size_t;

    /** Fills the storage with zeroes. */
    void clear();

private:
    explicit shared_datagram_buffer(datagram_buffer* iter);

    datagram_buffer* iter;
};

/** A simple allocator for datagram_buffers that maintains a free list. */
class datagram_buffer_cache {
public:
    friend class shared_datagram_buffer;

    datagram_buffer_cache() :
        free_buffers(nullptr) {}

    ~datagram_buffer_cache() {
        auto buf = free_buffers.load();
        while (buf) {
            auto next = buf->next;
            delete buf;
            buf = next;
        }
    }

    auto make_pending_buffer() -> shared_datagram_buffer {
        auto iter = free_buffers.load();
        if (iter) {
            while (!free_buffers.compare_exchange_strong(iter, iter->next));
            iter->next = nullptr;
        } else {
            iter = new datagram_buffer{};
            iter->cache = this;
        }

        assert(iter);
        assert(iter->cache == this);

        return shared_datagram_buffer{iter};
    }

private:
    void free_pending_buffer(datagram_buffer* iter) {
        assert(iter);
        assert(iter->refcount == 0);
        assert(iter->cache == this);

        auto first = free_buffers.load();
        iter->next = first;

        while (!free_buffers.compare_exchange_strong(first, iter)) {
            iter->next = first;
        }
    }

    std::atomic<datagram_buffer*> free_buffers;
};

inline shared_datagram_buffer::shared_datagram_buffer() : iter(nullptr) {}

inline shared_datagram_buffer::shared_datagram_buffer(shared_datagram_buffer&& other) :
    iter(std::exchange(other.iter, nullptr)) {
        assert(!iter || iter->refcount > 0);

        // other should always be left empty
        assert(!other.iter);
    }

inline shared_datagram_buffer::shared_datagram_buffer(const shared_datagram_buffer& other) :
    iter(other.iter) {
        if (iter) {
            assert(iter->refcount > 0);
            ++iter->refcount;
        }
    }

inline shared_datagram_buffer& shared_datagram_buffer::operator=(shared_datagram_buffer&& other) {
    assert(this != &other);

    // swap iters, the current iter will be released in other's destructor
    std::swap(iter, other.iter);

    assert(!iter || iter->refcount > 0);

    return *this;
}

inline shared_datagram_buffer& shared_datagram_buffer::operator=(const shared_datagram_buffer& other) {
    assert(!iter || iter->refcount > 0);

    // short-circuit if this and other have the same iter
    if (this == &other || iter == other.iter) {
        return *this;
    }

    // decrement refcount and maybe free the current iter
    if (iter && --iter->refcount == 0) {
        iter->cache->free_pending_buffer(iter);
    }

    // copy
    iter = other.iter;

    // increment refcount
    if (iter) {
        assert(iter->refcount > 0);
        ++iter->refcount;
    }

    // refcount must be a least 2: this and other
    assert(!iter || iter->refcount >= 2);

    return *this;
}

inline shared_datagram_buffer::~shared_datagram_buffer() {
    assert(!iter || iter->refcount > 0);

    // decrement refcount and maybe free iter
    if (iter && --iter->refcount == 0) {
        iter->cache->free_pending_buffer(iter);
    }
}

inline shared_datagram_buffer::operator bool() const {
    return bool(iter);
}

inline auto shared_datagram_buffer::buffer(std::size_t size) -> asio::mutable_buffer {
    assert(iter && iter->refcount > 0);
    return asio::buffer(iter->data, size);
}

inline auto shared_datagram_buffer::buffer(std::size_t size) const -> asio::const_buffer {
    assert(iter && iter->refcount > 0);
    return asio::buffer(iter->data, size);
}

inline auto shared_datagram_buffer::operator[](std::size_t i) -> char& {
    assert(iter && iter->refcount > 0);
    return iter->data[i];
}

inline auto shared_datagram_buffer::operator[](std::size_t i) const -> const char& {
    assert(iter && iter->refcount > 0);
    return iter->data[i];
}

inline auto shared_datagram_buffer::data() -> char* {
    assert(iter && iter->refcount > 0);
    return iter->data.data();
}

inline auto shared_datagram_buffer::data() const -> const char* {
    assert(iter && iter->refcount > 0);
    return iter->data.data();
}

inline auto shared_datagram_buffer::size() const -> std::size_t {
    assert(iter && iter->refcount > 0);
    return iter->data.size();
}

inline void shared_datagram_buffer::clear() {
    assert(iter && iter->refcount > 0);
    iter->data.fill('\0');
}

inline shared_datagram_buffer::shared_datagram_buffer(datagram_buffer* iter) :
    iter(iter) {
        assert(iter);
        assert(iter->refcount == 0);
        ++iter->refcount;
    }

} // namespace trellis
