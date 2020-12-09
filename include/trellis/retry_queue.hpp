#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <vector>

namespace trellis {

template <typename T, typename C, typename F = std::function<void(const T&)>>
class retry_queue {
public:
    using value_type = T;
    using timer_type = C;
    using clock = typename timer_type::clock_type;
    using duration = typename clock::duration;
    using time_point = typename clock::time_point;
    using callback_type = F;

    retry_queue(asio::io_context& io, callback_type cb) :
        queue(),
        timer(io),
        interval(std::chrono::milliseconds{50}),
        callback(cb) {}

    void push(const value_type& value) {
        push(value_type(value));
    }

    void push(value_type&& value) {
        queue.push_back({
            clock::now() + interval,
            std::move(value),
        });

        std::push_heap(queue.begin(), queue.end(), std::greater{});

        reset_timer();
    }

    template <typename G>
    bool remove_if(const G& pred) {
        if (queue.empty()) return false;

        auto iter = std::find_if(queue.begin(), queue.end(), [&](const auto& e) { return pred(e.value); });

        if (iter == queue.end()) return false;

        // Remove element from the queue, taking care to maintain the heap.

        if (iter == queue.end() - 1) {
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
                while (true) {
                    assert(i >= 0 && i < count);
                    assert(i_parent() == i || queue[i_parent()] <= queue[i]);

                    auto child = std::optional<int>{};

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
            assert(count == 1);
        } else {
            reset_timer();
        }

        return true;
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

    void reset_timer() {
        assert(!queue.empty());
        assert(std::is_heap(queue.begin(), queue.end(), std::greater{}));

        timer.expires_at(queue.front().when);

        timer.async_wait([this](asio::error_code ec) {
            if (ec == asio::error::operation_aborted) {
                return;
            }

            assert(!queue.empty());

            std::pop_heap(queue.begin(), queue.end(), std::greater{});

            auto& entry = queue.back();

            callback(entry.value);

            entry.when = clock::now() + interval;

            std::push_heap(queue.begin(), queue.end(), std::greater{});

            reset_timer();
        });
    }

    std::vector<retry_entry> queue;
    timer_type timer;
    duration interval;
    callback_type callback;
};

} // namespace trellis
