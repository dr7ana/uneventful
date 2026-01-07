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

    namespace detail {
        inline std::chrono::steady_clock::time_point get_time() {
            return std::chrono::steady_clock::now();
        }
    }  // namespace detail

}  // namespace un::event
