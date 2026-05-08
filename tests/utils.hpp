#pragma once

#include <uneventful.hpp>

#include <catch2/catch_test_macros.hpp>

#include <mutex>
#include <utility>

namespace un::event::test {

    using namespace un::log::literals;

    using test_loop = event_loop<>;
    static_assert(test_loop::using_default_channel == true);

    struct test_helper {
        template <typename T, typename... Args>
        static auto make_shared(test_loop& loop, Args&&... args) {
            return loop.template make_shared<T>(std::forward<Args>(args)...);
        }

        template <typename T, typename Callable>
        static auto shared_ptr(test_loop& loop, T* obj, Callable&& deleter) {
            return loop.template shared_ptr<T>(obj, std::forward<Callable>(deleter));
        }

        template <typename... Callables>
        static void queue_jobs(test_loop& loop, Callables&&... callables) {
            {
                std::lock_guard lock{loop.job_queue_mutex};
                (loop.job_queue.emplace_back(std::forward<Callables>(callables)), ...);
            }

            event_active(loop.job_waker.get(), 0, 0);
        }
    };

}  // namespace un::event::test
