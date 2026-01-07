#pragma once

#include <uneventful.hpp>

#include <catch2/catch_test_macros.hpp>

#include <utility>

namespace un::event::test {

    using namespace un::log::literals;

    struct test_helper {
        template <typename T, typename... Args>
        static auto make_shared(event_loop& loop, Args&&... args) {
            return loop.template make_shared<T>(std::forward<Args>(args)...);
        }

        template <typename T, typename Callable>
        static auto shared_ptr(event_loop& loop, T* obj, Callable&& deleter) {
            return loop.template shared_ptr<T>(obj, std::forward<Callable>(deleter));
        }
    };

}  // namespace un::event::test
