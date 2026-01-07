#include "utils.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

namespace un::event::test {
    TEST_CASE("event_loop constructs with backend", "[event_loop][construction]") {
        auto loop = un::event::event_loop::make();
        REQUIRE(loop);
        REQUIRE(loop->loop());

        const char* method = event_base_get_method(loop->loop().get());
        REQUIRE(method != nullptr);
        REQUIRE(method[0] != '\0');
    }

    TEST_CASE("event_loop thread identity basics", "[event_loop][thread]") {
        auto loop = un::event::event_loop::make();
        auto main_id = std::this_thread::get_id();

        REQUIRE_FALSE(loop->in_event_loop());

        auto loop_id = loop->call_get([] { return std::this_thread::get_id(); });
        REQUIRE(loop_id != main_id);

        auto in_loop = loop->call_get([&] { return loop->in_event_loop(); });
        REQUIRE(in_loop);
    }

    TEST_CASE("event_loop executes call on loop thread", "[event_loop][call]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        REQUIRE_FALSE(loop->in_event_loop());

        std::promise<bool> p;
        auto fut = p.get_future();

        loop->call([&] { p.set_value(loop->in_event_loop()); });

        REQUIRE(fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(fut.get());
    }

    TEST_CASE("event_loop executes call inline on loop thread", "[event_loop][call]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::promise<bool> p;
        auto fut = p.get_future();

        loop->call_soon([&] {
            bool ran_inline = false;
            loop->call([&] { ran_inline = true; });
            p.set_value(ran_inline);
        });

        REQUIRE(fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(fut.get());
    }

    TEST_CASE("event_loop returns value from call_get", "[event_loop][call_get]") {
        auto loop = un::event::event_loop::make();

        auto value = loop->call_get([] { return 42; });
        REQUIRE(value == 42);

        auto in_loop = loop->call_get([&] { return loop->in_event_loop(); });
        REQUIRE(in_loop);
    }

    TEST_CASE("event_loop propagates call_get exceptions", "[event_loop][call_get]") {
        auto loop = un::event::event_loop::make();

        REQUIRE_THROWS_AS(loop->call_get([]() -> int { throw std::runtime_error("boom"); }), std::runtime_error);
    }

    TEST_CASE("event_loop executes call_soon on the loop thread", "[event_loop][call_soon]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        REQUIRE_FALSE(loop->in_event_loop());

        std::promise<bool> p;
        auto fut = p.get_future();

        loop->call_soon([&] { p.set_value(loop->in_event_loop()); });

        REQUIRE(fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(fut.get());
    }

    TEST_CASE("event_loop executes call_soon in FIFO order", "[event_loop][call_soon]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::vector<int> order;

        std::promise<void> p;
        auto fut = p.get_future();

        loop->call_soon([&] { order.push_back(1); });
        loop->call_soon([&] { order.push_back(2); });
        loop->call_soon([&] {
            order.push_back(3);
            p.set_value();
        });

        REQUIRE(fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(order == std::vector<int>{1, 2, 3});
    }

    TEST_CASE("event_loop executes call_soon enqueued from a callback", "[event_loop][call_soon]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::vector<char> order;

        std::promise<void> p;
        auto fut = p.get_future();

        loop->call_soon([&] {
            order.push_back('A');
            loop->call_soon([&] {
                order.push_back('B');
                p.set_value();
            });
        });

        REQUIRE(fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(order == std::vector<char>{'A', 'B'});
    }

    TEST_CASE("event_loop handles call_soon from multiple threads", "[event_loop][call_soon]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();

        constexpr int threads = 4;
        constexpr int tasks_per_thread = 8;
        constexpr int expected = threads * tasks_per_thread;

        std::atomic<int> count{0};
        std::atomic<bool> done{false};
        std::promise<void> p;
        auto fut = p.get_future();

        std::vector<std::thread> producers;
        producers.reserve(threads);
        for (int t = 0; t < threads; ++t) {
            producers.emplace_back([&] {
                for (int i = 0; i < tasks_per_thread; ++i) {
                    loop->call_soon([&] {
                        if (count.fetch_add(1) + 1 == expected) {
                            if (!done.exchange(true))
                                p.set_value();
                        }
                    });
                }
            });
        }

        for (auto& t : producers)
            t.join();

        REQUIRE(fut.wait_for(1s) == std::future_status::ready);
        REQUIRE(count.load() == expected);
    }

    TEST_CASE("event_loop executes call_get inline on loop thread", "[event_loop][call_get]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::promise<bool> p;
        auto fut = p.get_future();

        loop->call_soon([&] {
            auto value = loop->call_get([] { return 7; });
            p.set_value(value == 7 && loop->in_event_loop());
        });

        REQUIRE(fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(fut.get());
    }

    TEST_CASE("event_loop executes call_get with void return", "[event_loop][call_get]") {
        auto loop = un::event::event_loop::make();
        std::atomic<bool> ran{false};

        loop->call_get([&] { ran.store(true); });

        REQUIRE(ran.load());
    }
}  // namespace un::event::test
