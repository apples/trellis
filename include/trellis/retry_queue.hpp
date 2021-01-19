#pragma once

#include "guarded_timer.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <vector>

namespace trellis {

/** Implements a time-delayed priority queue. */
template <typename Value, typename Guard, typename Executor, typename Timer = guarded_timer<Guard>, typename Handler = std::function<void(const Value&)>>
class retry_queue {
public:
    using value_type = Value;
    using guard_type = Guard;
    using executor_type = Executor;
    using timer_type = Timer;
    using callback_type = Handler;
    using clock = typename timer_type::clock_type;
    using duration = typename clock::duration;
    using time_point = typename clock::time_point;
    using guard_ptr = std::weak_ptr<guard_type>;

    retry_queue(asio::io_context& io, executor_type ex, callback_type cb) :
        queue(),
        timer(ex),
        interval(std::chrono::milliseconds{50}),
        callback(std::move(cb)) {}

    void push(const value_type& value, const guard_ptr& guard) {
        push(value_type(value), guard);
    }

    void push(value_type&& value, const guard_ptr& guard) {
        queue.push_back({
            clock::now() + interval,
            std::move(value),
        });

        std::push_heap(queue.begin(), queue.end(), std::greater{});
        assert(std::is_heap(queue.begin(), queue.end(), std::greater{}));

        reset_timer(guard);
    }

    /** Removes the first instance of a queue item where the predicate returns true. */
    template <typename G>
    bool remove_all_if(const G& pred, const guard_ptr& guard) {
        if (queue.empty()) return false;

        std::sort(queue.begin(), queue.end());

        auto iter = std::remove_if(queue.begin(), queue.end(), [&](const auto& e) { return pred(e.value); });

        auto success = iter != queue.end();

        queue.erase(iter, queue.end());

        assert(std::is_heap(queue.begin(), queue.end(), std::greater{}));

        // If the queue is empty, the timer needs to be cancelled, since it assumes there's a waiting entry.

        if (queue.empty()) {
            [[maybe_unused]] auto count = timer.cancel();
            // The pending timer completion handler might have already been queued for execution.
            // In this case, it cannot be cancelled, so count will be zero.
            assert(count <= 1);
        } else {
            reset_timer(guard);
        }

        return success;
    }

    /** Removes the first instance of a queue item where the predicate returns true. */
    template <typename G>
    bool remove_one_if(const G& pred, const guard_ptr& guard) {
        if (queue.empty()) return false;

        auto iter = std::find_if(queue.begin(), queue.end(), [&](const auto& e) { return pred(e.value); });

        if (iter == queue.end()) return false;

        // Remove element from the queue, taking care to maintain the heap.

        if (iter == queue.end() - 1) {
            // Fast path in the case that we need to remove the last element.
            queue.pop_back();
        } else {
            auto i = iter - queue.begin();

            std::swap(queue[i], queue.back());
            queue.pop_back();

            auto count = std::ptrdiff_t(queue.size());

            auto i_parent = [&]{ return (i - 1) / 2; };
            auto i_left = [&]{ return i * 2 + 1; };
            auto i_right = [&]{ return i * 2 + 2; };

            if (i_parent() != i && i_parent() < count && queue[i] <= queue[i_parent()]) {
                // Filter up
                while (i_parent() != i && queue[i] < queue[i_parent()]) {
                    assert(i >= 0);
                    assert(i < count);
                    assert(i_parent() >= 0);
                    assert(i_parent() != i);
                    assert(i_parent() < i);
                    assert(i_left() >= count || queue[i] <= queue[i_left()]);
                    assert(i_right() >= count || queue[i] <= queue[i_right()]);

                    std::swap(queue[i], queue[i_parent()]);
                    i = i_parent();
                }
            } else {
                // Filter down
                while (true) {
                    assert(i >= 0 && i < count);
                    assert(i_parent() == i || queue[i_parent()] <= queue[i]);

                    auto child = std::optional<std::ptrdiff_t>{};

                    if (i_left() < count && queue[i_left()] < queue[i]) {
                        child = i_left();
                    }

                    if (i_right() < count && queue[i_right()] < queue[child ? *child : i]) {
                        child = i_right();
                    }

                    if (child) {
                        assert(*child > i && *child < count);
                        std::swap(queue[i], queue[*child]);
                        i = *child;
                    } else {
                        break;
                    }
                }
            }
        }

        assert(std::is_heap(queue.begin(), queue.end(), std::greater{}));

        // If the queue is empty, the timer needs to be cancelled, since it assumes there's a waiting entry.

        if (queue.empty()) {
            [[maybe_unused]] auto count = timer.cancel();
            // The pending timer completion handler might have already been queued for execution.
            // In this case, it cannot be cancelled, so count will be zero.
            assert(count <= 1);
        } else {
            reset_timer(guard);
        }

        return true;
    }

    auto size() const -> std::size_t {
        return queue.size();
    }

private:
    struct retry_entry {
        time_point when;
        value_type value;

        constexpr bool operator<(const retry_entry& other) const {
            return when < other.when;
        }

        constexpr bool operator>(const retry_entry& other) const {
            return when > other.when;
        }

        constexpr bool operator<=(const retry_entry& other) const {
            return when <= other.when;
        }

        constexpr bool operator>=(const retry_entry& other) const {
            return when >= other.when;
        }
    };

    struct life_token {};

    void reset_timer(const guard_ptr& guard) {
        assert(guard.lock());
        assert(!queue.empty());
        assert(std::is_heap(queue.begin(), queue.end(), std::greater{}));

        [[maybe_unused]] auto cancelled = timer.expires_at(queue.front().when);
        assert(cancelled <= 1);

        timer.async_wait(guard, [this](asio::error_code ec, const guard_ptr& weak_guard) {
            if (ec == asio::error::operation_aborted) {
                return;
            }

            assert(!ec);

            // This completion handler might have already been queued before the timer was cancelled.
            // If that happens, the retry queue will be empty.
            if (queue.empty()) {
                return;
            }

            assert(std::is_heap(queue.begin(), queue.end(), std::greater{}));

            std::pop_heap(queue.begin(), queue.end(), std::greater{});

            auto& entry = queue.back();

            assert(weak_guard.lock());

            callback(entry.value);

            if (weak_guard.lock()) {
                entry.when = clock::now() + interval;

                std::push_heap(queue.begin(), queue.end(), std::greater{});

                assert(std::is_heap(queue.begin(), queue.end(), std::greater{}));

                reset_timer(weak_guard);
            }
        });
    }

    std::vector<retry_entry> queue;
    timer_type timer;
    duration interval;
    callback_type callback;
};

} // namespace trellis
