#pragma once

#include <unlog.hpp>

#include <algorithm>
#include <cstdint>
#include <span>

namespace un::event {
    using cspan = unlog::cspan;
    using uspan = unlog::uspan;
    using bspan = unlog::bspan;

    using namespace unlog::literals;
    using namespace un::log::operators;
    using namespace std::chrono_literals;

    inline constexpr size_t thread_bufsize_bytes{1U << 22};

    inline timeval loop_time_to_timeval(std::chrono::microseconds t) {
        return timeval{
                .tv_sec = static_cast<decltype(timeval::tv_sec)>(t / 1s),
                .tv_usec = static_cast<decltype(timeval::tv_usec)>((t % 1s) / 1us)};
    }

    namespace detail {
        inline std::chrono::steady_clock::time_point get_time() {
            return std::chrono::steady_clock::now();
        }
    }  // namespace detail

}  // namespace un::event
