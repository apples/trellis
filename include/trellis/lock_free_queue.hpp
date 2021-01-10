#pragma once

#include <atomic>
#include <cassert>
#include <optional>
#include <type_traits>

namespace trellis {

template <typename T>
class lock_free_queue {
public:
    using value_type = T;
    using storage_type = std::aligned_storage_t<sizeof(value_type), alignof(value_type)>;

    struct list_node {
        storage_type storage;
        list_node* next = nullptr;
    };

    lock_free_queue() :
        _first(nullptr),
        _free_list(nullptr),
        _before_next(nullptr),
        _last(nullptr) {}

    ~lock_free_queue() {
        if (!_first) {
            return;
        }

        auto before_next = _before_next.load(std::memory_order_acquire);
        auto last = _last.load(std::memory_order_acquire);

        assert(before_next);
        assert(last);

        auto next = before_next->next;

        for (auto node = _first; node != next;) {
            assert(node);
            auto tmp = node->next;
            delete node;
            node = tmp;
        }

        for (auto node = next; node;) {
            assert(node);
            auto& value = *std::launder(reinterpret_cast<value_type*>(&node->storage));
            value.~value_type();

            auto tmp = node->next;
            delete node;
            node = tmp;
        }

        for (auto node = _free_list; node;) {
            assert(node);
            auto tmp = node->next;
            delete node;
            node = tmp;
        }
    }

    void push(T t) {
        auto node = [&]{
            auto node = _free_list;

            if (node) {
                _free_list = node->next;
            } else {
                node = new list_node{};
            }

            new (&node->storage) T(std::move(t));

            return node;
        }();

        auto last = _last.load(std::memory_order_acquire);

        if (!last) {
            _first = new list_node{{}, node};
            _before_next.store(_first, std::memory_order_relaxed);
            _last.store(node, std::memory_order_release);
        } else {
            last->next = node;
            _last.store(node, std::memory_order_release);
        }

        // cleanup
        auto before_next = _before_next.load(std::memory_order_acquire);
        assert(before_next);
        while (_first != before_next) {
            assert(_first);
            auto node = _first;
            _first = node->next;
            node->next = _free_list;
            _free_list = node;
        }
    }

    auto pop() -> std::optional<value_type> {
        auto last = _last.load(std::memory_order_acquire);

        if (!last) {
            return std::nullopt;
        }

        auto before_next = _before_next.load(std::memory_order_relaxed);

        if (before_next == last) {
            return std::nullopt;
        }

        auto& value = *std::launder(reinterpret_cast<value_type*>(&before_next->next->storage));

        auto result = std::optional<value_type>(std::move(value));

        value.~value_type();

        _before_next.store(before_next->next, std::memory_order_release);

        return result;
    }

private:
    list_node* _first;
    list_node* _free_list;
    std::atomic<list_node*> _before_next;
    std::atomic<list_node*> _last;
};

} // namespace trellis
