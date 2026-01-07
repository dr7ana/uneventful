#include "utils.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

namespace un::event::test {
    TEST_CASE("event_loop executes call_later once from non-loop thread", "[event_loop][call_later]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::atomic<int> count{0};

        std::promise<void> done;
        auto done_fut = done.get_future();
        std::atomic<bool> done_set{false};

        loop->call_later(10ms, [&] {
            if (count.fetch_add(1) + 1 == 1) {
                if (!done_set.exchange(true))
                    done.set_value();
            }
        });

        REQUIRE(done_fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(count.load() == 1);

        std::promise<bool> stable;
        auto stable_fut = stable.get_future();
        loop->call_later(50ms, [&] { stable.set_value(count.load() == 1); });

        REQUIRE(stable_fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(stable_fut.get());
    }

    TEST_CASE("event_loop executes call_later immediately when overdue", "[event_loop][call_later]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::promise<bool> p;
        auto fut = p.get_future();

        loop->call_later(0us, [&] { p.set_value(loop->in_event_loop()); });

        REQUIRE(fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(fut.get());
    }

    TEST_CASE("event_loop defers call_later invoked on loop thread", "[event_loop][call_later]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::promise<int> p;
        auto fut = p.get_future();
        std::atomic<int> stage{0};

        loop->call_soon([&] {
            stage.store(1);
            loop->call_later(0us, [&] { p.set_value(stage.load()); });
            stage.store(2);
        });

        REQUIRE(fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(fut.get() == 2);
    }

    TEST_CASE("event_loop executes multiple call_later one-shots", "[event_loop][call_later]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::atomic<int> count{0};

        std::promise<void> done;
        auto done_fut = done.get_future();
        std::atomic<bool> done_set{false};

        auto bump = [&] {
            if (count.fetch_add(1) + 1 == 3) {
                if (!done_set.exchange(true))
                    done.set_value();
            }
        };

        loop->call_later(5ms, bump);
        loop->call_later(10ms, bump);
        loop->call_later(15ms, bump);

        REQUIRE(done_fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(count.load() == 3);
    }

    TEST_CASE("event_loop executes call_later timers with same delay", "[event_loop][call_later]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::atomic<int> count{0};

        std::promise<void> done;
        auto done_fut = done.get_future();
        std::atomic<bool> done_set{false};

        auto bump = [&] {
            if (count.fetch_add(1) + 1 == 2) {
                if (!done_set.exchange(true))
                    done.set_value();
            }
        };

        loop->call_later(10ms, bump);
        loop->call_later(10ms, bump);

        REQUIRE(done_fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(count.load() == 2);
    }

    TEST_CASE("event_loop handles call_later from multiple threads", "[event_loop][call_later]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();

        constexpr int threads = 4;
        constexpr int tasks_per_thread = 6;
        constexpr int expected = threads * tasks_per_thread;

        std::atomic<int> count{0};
        std::promise<void> done;
        auto done_fut = done.get_future();
        std::atomic<bool> done_set{false};

        std::vector<std::thread> producers;
        producers.reserve(threads);
        for (int t = 0; t < threads; ++t) {
            producers.emplace_back([&] {
                for (int i = 0; i < tasks_per_thread; ++i) {
                    loop->call_later(0us, [&] {
                        if (count.fetch_add(1) + 1 == expected) {
                            if (!done_set.exchange(true))
                                done.set_value();
                        }
                    });
                }
            });
        }

        for (auto& t : producers)
            t.join();

        REQUIRE(done_fut.wait_for(1s) == std::future_status::ready);
        REQUIRE(count.load() == expected);
    }

    TEST_CASE("event_loop call_every starts immediately by default", "[event_loop][call_every]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::atomic<int> count{0};
        std::promise<void> ran;
        auto ran_fut = ran.get_future();
        std::atomic<bool> ran_set{false};

        auto watcher = loop->call_every(10ms, [&] {
            if (count.fetch_add(1) + 1 == 1) {
                if (!ran_set.exchange(true))
                    ran.set_value();
            }
        });

        REQUIRE(watcher->is_running());
        REQUIRE(ran_fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(count.load() >= 1);

        REQUIRE(watcher->stop());
    }

    TEST_CASE("event_loop call_every wait mode avoids reentry", "[event_loop][call_every]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::atomic<bool> in_callback{false};
        std::atomic<bool> reentered{false};
        std::atomic<int> count{0};

        std::promise<void> done;
        auto done_fut = done.get_future();
        std::atomic<bool> done_set{false};

        std::shared_ptr<un::event::ev_watcher> watcher;
        watcher = loop->call_every(
                10ms,
                [&] {
                    if (in_callback.exchange(true))
                        reentered.store(true);

                    std::this_thread::sleep_for(30ms);
                    in_callback.store(false);

                    if (count.fetch_add(1) + 1 >= 2) {
                        watcher->stop();
                        if (!done_set.exchange(true))
                            done.set_value();
                    }
                },
                true,
                true);

        REQUIRE(done_fut.wait_for(500ms) == std::future_status::ready);
        REQUIRE_FALSE(reentered.load());
    }

    TEST_CASE("event_loop call_every respects start/stop semantics", "[event_loop][call_every]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::atomic<int> count{0};

        std::promise<void> ran;
        auto ran_fut = ran.get_future();
        std::atomic<bool> ran_set{false};

        auto watcher = loop->call_every(
                10ms,
                [&] {
                    if (count.fetch_add(1) + 1 >= 2) {
                        if (!ran_set.exchange(true))
                            ran.set_value();
                    }
                },
                false);

        REQUIRE_FALSE(watcher->is_running());
        REQUIRE_FALSE(watcher->stop());

        std::promise<bool> idle;
        auto idle_fut = idle.get_future();
        loop->call_later(30ms, [&] { idle.set_value(count.load() == 0); });

        REQUIRE(idle_fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(idle_fut.get());

        REQUIRE(watcher->start());
        REQUIRE_FALSE(watcher->start());

        REQUIRE(ran_fut.wait_for(200ms) == std::future_status::ready);

        REQUIRE(watcher->stop());
        REQUIRE_FALSE(watcher->stop());

        int stopped_at = count.load();
        std::promise<bool> stable;
        auto stable_fut = stable.get_future();
        loop->call_later(50ms, [&] { stable.set_value(count.load() == stopped_at); });

        REQUIRE(stable_fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(stable_fut.get());
    }

    TEST_CASE("event_loop call_every can stop from callback", "[event_loop][call_every]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::atomic<int> count{0};

        std::promise<void> stopped;
        auto stopped_fut = stopped.get_future();
        std::atomic<bool> stopped_set{false};

        std::shared_ptr<un::event::ev_watcher> watcher;
        watcher = loop->call_every(
                10ms,
                [&] {
                    if (count.fetch_add(1) + 1 == 1) {
                        watcher->stop();
                        if (!stopped_set.exchange(true))
                            stopped.set_value();
                    }
                },
                false);

        REQUIRE(watcher->start());
        REQUIRE(stopped_fut.wait_for(200ms) == std::future_status::ready);

        int stopped_at = count.load();
        std::promise<bool> stable;
        auto stable_fut = stable.get_future();
        loop->call_later(50ms, [&] { stable.set_value(count.load() == stopped_at); });

        REQUIRE(stable_fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(stable_fut.get());
    }

    TEST_CASE("event_loop call_every non-wait cadence stays near interval", "[event_loop][call_every]") {
        using namespace std::chrono_literals;

        using clock = std::chrono::steady_clock;
        constexpr auto interval = 20ms;
        constexpr int samples = 5;

        auto loop = un::event::event_loop::make();
        std::vector<clock::time_point> times;
        times.reserve(samples);

        std::promise<void> done;
        auto done_fut = done.get_future();
        std::atomic<bool> done_set{false};

        std::shared_ptr<un::event::ev_watcher> watcher;
        watcher = loop->call_every(
                interval,
                [&] {
                    times.push_back(clock::now());
                    if (times.size() >= samples) {
                        watcher->stop();
                        if (!done_set.exchange(true))
                            done.set_value();
                    }
                },
                true,
                false);

        REQUIRE(done_fut.wait_for(500ms) == std::future_status::ready);
        REQUIRE(times.size() >= samples);

        auto duration = times.back() - times.front();
        auto expected = interval * (samples - 1);
        REQUIRE(duration <= expected + 120ms);
    }

    TEST_CASE("event_loop call_every continues after callback exception", "[event_loop][call_every]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::atomic<int> count{0};
        std::atomic<bool> threw{false};

        std::promise<void> done;
        auto done_fut = done.get_future();
        std::atomic<bool> done_set{false};

        std::shared_ptr<un::event::ev_watcher> watcher;
        watcher = loop->call_every(
                10ms,
                [&] {
                    int idx = count.fetch_add(1);
                    if (idx == 0) {
                        threw.store(true);
                        throw std::runtime_error("boom");
                    }

                    if (idx >= 1) {
                        watcher->stop();
                        if (!done_set.exchange(true))
                            done.set_value();
                    }
                },
                true,
                false);

        REQUIRE(done_fut.wait_for(500ms) == std::future_status::ready);
        REQUIRE(threw.load());
        REQUIRE(count.load() >= 2);
    }

    TEST_CASE("event_loop call_every wait mode delays callbacks after work", "[event_loop][call_every]") {
        using namespace std::chrono_literals;

        auto measure_gap = [](bool wait) {
            using clock = std::chrono::steady_clock;

            auto loop = un::event::event_loop::make();
            std::promise<void> done;
            auto fut = done.get_future();

            clock::time_point end_first{};
            clock::time_point start_second{};
            std::atomic<int> count{0};
            std::shared_ptr<un::event::ev_watcher> watcher;

            constexpr auto interval = 40ms;
            constexpr auto work = 60ms;

            watcher = loop->call_every(
                    interval,
                    [&] {
                        int idx = count.fetch_add(1);
                        if (idx == 0) {
                            std::this_thread::sleep_for(work);
                            end_first = clock::now();
                        }
                        else if (idx == 1) {
                            start_second = clock::now();
                            watcher->stop();
                            done.set_value();
                        }
                    },
                    true,
                    wait);

            REQUIRE(fut.wait_for(2s) == std::future_status::ready);
            return start_second - end_first;
        };

        auto gap_no_wait = measure_gap(false);
        auto gap_wait = measure_gap(true);

        REQUIRE(gap_wait >= gap_no_wait + 30ms);
    }

    TEST_CASE("event_loop cancels call_later after destruction", "[event_loop][call_later]") {
        using namespace std::chrono_literals;

        std::atomic<int> count{0};

        {
            auto loop = un::event::event_loop::make();
            loop->call_later(100ms, [&] { count.fetch_add(1); });
        }

        std::this_thread::sleep_for(150ms);
        REQUIRE(count.load() == 0);
    }

    TEST_CASE("event_loop destructor stops tickers", "[event_loop][lifecycle]") {
        using namespace std::chrono_literals;

        std::atomic<int> count{0};
        int stopped_at = 0;

        {
            auto loop = un::event::event_loop::make();
            std::promise<void> ran;
            auto ran_fut = ran.get_future();
            std::atomic<bool> ran_set{false};

            auto watcher = loop->call_every(10ms, [&] {
                if (count.fetch_add(1) + 1 >= 2) {
                    if (!ran_set.exchange(true))
                        ran.set_value();
                }
            });

            REQUIRE(ran_fut.wait_for(200ms) == std::future_status::ready);
            stopped_at = count.load();
        }

        std::this_thread::sleep_for(50ms);
        REQUIRE(count.load() == stopped_at);
    }

    struct deleter_probe {
        std::promise<void>* done;
        std::thread::id* thread_id;
        bool* in_loop;
        un::event::event_loop* loop;

        ~deleter_probe() {
            if (thread_id)
                *thread_id = std::this_thread::get_id();
            if (in_loop && loop)
                *in_loop = loop->in_event_loop();
            if (done)
                done->set_value();
        }
    };

    TEST_CASE("event_loop runs loop_deleter on loop thread", "[event_loop][deleter]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::promise<void> done;
        auto fut = done.get_future();
        std::thread::id dtor_id;
        bool in_loop = false;

        {
            auto ptr = test_helper::make_shared<deleter_probe>(*loop, &done, &dtor_id, &in_loop, loop.get());
            (void)ptr;
        }

        REQUIRE(fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(in_loop);

        auto loop_id = loop->call_get([] { return std::this_thread::get_id(); });
        REQUIRE(dtor_id == loop_id);
    }

    TEST_CASE("event_loop runs wrapped_deleter on loop thread", "[event_loop][deleter]") {
        using namespace std::chrono_literals;

        auto loop = un::event::event_loop::make();
        std::promise<void> done;
        auto fut = done.get_future();
        std::thread::id dtor_id;
        bool in_loop = false;

        {
            auto ptr = test_helper::shared_ptr(*loop, new int{1}, [&](int* value) {
                dtor_id = std::this_thread::get_id();
                in_loop = loop->in_event_loop();
                delete value;
                done.set_value();
            });
            (void)ptr;
        }

        REQUIRE(fut.wait_for(200ms) == std::future_status::ready);
        REQUIRE(in_loop);

        auto loop_id = loop->call_get([] { return std::this_thread::get_id(); });
        REQUIRE(dtor_id == loop_id);
    }
}  // namespace un::event::test
