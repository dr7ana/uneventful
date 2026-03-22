#pragma once

#include <uneventful.hpp>

#include <catch2/catch_test_macros.hpp>

#include <mutex>
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

        template <typename... Callables>
        static void queue_jobs(event_loop& loop, Callables&&... callables) {
            {
                std::lock_guard lock{loop.job_queue_mutex};
                (loop.job_queue.emplace(std::forward<Callables>(callables)), ...);
            }

            event_active(loop.job_waker.get(), 0, 0);
        }
    };

}  // namespace un::event::test
