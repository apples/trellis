#pragma once

#include <asio.hpp>

#include <memory>
#include <utility>

namespace trellis::_detail {

template <typename Handler, typename GuardValue>
auto guarded_timer_invoke(const Handler& handler, asio::error_code ec, const std::weak_ptr<GuardValue>& guard) -> decltype(handler(ec, guard)) {
    return handler(ec, guard);
};

template <typename Handler, typename GuardValue>
auto guarded_timer_invoke(const Handler& handler, asio::error_code ec, [[maybe_unused]] const std::weak_ptr<GuardValue>& guard) -> decltype(handler(ec)) {
    return handler(ec);
};

template <typename GuardValue = void>
class guarded_timer {
public:
    using guard_value = GuardValue;
    using underlying_timer_type = asio::steady_timer;
    using executor_type = underlying_timer_type::executor_type;
    using clock_type = underlying_timer_type::clock_type;
    using duration = underlying_timer_type::duration;
    using time_point = underlying_timer_type::time_point;

    explicit guarded_timer(const executor_type& executor) : timer(executor) {}

    auto expires_at(const time_point& expiry_time) {
        return timer.expires_at(expiry_time);
    }

    auto expires_from_now(const duration& expiry_time) {
        return timer.expires_from_now(expiry_time);
    }

    auto cancel() -> std::size_t {
        return timer.cancel();
    }

    template <typename WaitHandler>
    auto async_wait(const std::weak_ptr<guard_value>& guard, WaitHandler&& handler) {
        assert(guard.lock());

        return timer.async_wait([guard, handler = std::forward<WaitHandler>(handler)](asio::error_code ec) {
            if (!guard.lock()) return;
            guarded_timer_invoke(handler, ec, guard);
        });
    }

private:
    underlying_timer_type timer;
};

} // namespace trellis::_detail
