#pragma once

#include "utils.hpp"

extern "C" {
#include <event2/event.h>
#include <event2/thread.h>
}

#include <atomic>
#include <cstdint>
#include <future>
#include <list>
#include <memory>
#include <queue>
#include <thread>

namespace un::event {
    namespace deleters {
        struct _event {
            inline void operator()(::event* e) const {
                if (e) {
                    ::event_free(e);
                }
            }
        };
    }  // namespace deleters

    namespace test {
        struct test_helper;
    }  // namespace test

    namespace detail {
        struct event_base* try_make_et_evbase();
    }  // namespace detail

    using job_hook = std::function<void()>;

    using job_deque =
            std::deque<job_hook, allocazam::allocazam_std_allocator<job_hook, allocazam::memory_mode::dynamic>>;

    using event_ptr = std::unique_ptr<::event, deleters::_event>;

    using caller_id_t = uint16_t;

    template <auto& C>
    class unevent_loop final : public std::enable_shared_from_this<unevent_loop<C>> {
        using ev_channel_type = std::remove_cvref_t<decltype(C)>;
        static_assert(unlog::channel_type<ev_channel_type>);

        static constexpr auto& log = C;

        unevent_loop() : ev_loop{detail::try_make_et_evbase(), ::event_base_free} {
            unlog::trace(log, "Beginning loop context creation with new ev loop thread");

            unlog::debug(log, "Started libevent loop with backend {}", event_base_get_method(ev_loop.get()));

            setup_job_waker();

            std::promise<void> p;

            loop_thread = std::thread{[this, &p]() mutable {
                unlog::debug(log, "Starting event loop run");
                p.set_value();
                event_base_loop(ev_loop.get(), EVLOOP_NO_EXIT_ON_EMPTY);
                unlog::debug(log, "Event loop run returned, thread finished");
            }};

            loop_thread_id = loop_thread.get_id();
            p.get_future().get();

            running.store(true);
            unlog::info(log, "loop is started");
        }

        unevent_loop(const unevent_loop&) = delete;
        unevent_loop(unevent_loop&&) = delete;
        unevent_loop& operator=(unevent_loop&&) = delete;
        unevent_loop& operator=(unevent_loop) = delete;

      public:
        [[nodiscard]] static std::shared_ptr<unevent_loop> make() {
            return std::shared_ptr<unevent_loop>{new unevent_loop{}};
        }

        ~unevent_loop() {
            unlog::info(log, "Shutting down loop...");

            call_get([this]() { shutdown(); });

            stop_thread();

            job_waker.reset();
            unlog::info(log, "Loop shutdown complete");
        }

        struct ev_watcher {
            friend class unevent_loop;
            friend struct loop_callbacks;

          private:
            event_ptr ev;
            timeval interval;
            std::function<void()> f;

            void init_event(
                    ::event_base* _loop,
                    std::chrono::microseconds _t,
                    std::function<void()> task,
                    bool one_off = false,
                    bool start_immediately = true) {
                f = std::move(task);

                interval = loop_time_to_timeval(_t);

                ev.reset(event_new(
                        _loop,
                        -1,
                        one_off ? 0 : EV_PERSIST,
                        [](evutil_socket_t, short, void* s) {
                            try {
                                auto* self = reinterpret_cast<unevent_loop::ev_watcher*>(s);
                                if (not self->f) {
                                    unlog::critical(log, "Ticker does not have a callback to execute!");
                                    return;
                                }
                                // execute callback
                                self->f();
                            } catch (const std::exception& e) {
                                unlog::critical(log, "Ticker caught exception: {}", e.what());
                            }
                        },
                        this));

                if ((one_off or start_immediately) and not start()) {
                    unlog::critical(log, "Failed to immediately start one-off event!");
                }
            }

            ev_watcher() = default;

          public:
            ~ev_watcher() {
                ev.reset();
                f = nullptr;
            }

            /** Starts the repeating event on the given interval on Ticker creation
                Returns:
                    - true: event successfully started
                    - false: event is already running, or failed to start the event
             */
            bool start() {
                if (event_add(ev.get(), &interval) != 0) {
                    unlog::critical(log, "EventHandler failed to start repeating event!");
                    return false;
                }

                return true;
            }

            /** Stops the repeating event managed by Ticker
                Returns:
                    - true: event successfully stopped
                    - false: event is already stopped, or failed to stop the event
             */
            bool stop() {
                if (ev && event_del(ev.get()) != 0) {
                    unlog::critical(log, "EventHandler failed to pause repeating event!");
                    return false;
                }

                return true;
            }
        };

      private:
        std::atomic<bool> running{false};
        std::unique_ptr<::event_base, void (*)(struct ::event_base*)> ev_loop;
        std::thread loop_thread;
        std::thread::id loop_thread_id;

        event_ptr job_waker;
        job_deque job_queue;
        std::mutex job_queue_mutex;

        std::unordered_map<caller_id_t, std::list<std::weak_ptr<ev_watcher>>> tickers;

      public:
        ::event_base* loop() const noexcept { return ev_loop.get(); }

        template <std::invocable<> Callable>
        void call(Callable&& f) {
            if (in_event_loop()) {
                f();
            }
            else {
                call_soon(std::forward<Callable>(f));
            }
        }

        template <typename Callable, typename Ret = decltype(std::declval<Callable>()())>
        Ret call_get(Callable&& f) {
            if (in_event_loop()) {
                return f();
            }

            std::promise<Ret> prom;
            auto fut = prom.get_future();

            call_soon([&f, &prom] {
                try {
                    if constexpr (!std::is_void_v<Ret>) {
                        prom.set_value(f());
                    }
                    else {
                        f();
                        prom.set_value();
                    }
                } catch (...) {
                    prom.set_exception(std::current_exception());
                }
            });

            return fut.get();
        }

        /** This invocation of `call_every` will return an EventHandler object from which the
           application can start and stop the repeated event. It is NOT tied to the lifetime of the
           caller via a weak_ptr.

            Configurable parameters:
                - start_immediately : will call ::event_add() before returning the ticker
        */
        template <typename Callable>
        [[nodiscard]] std::shared_ptr<ev_watcher> call_every(
                std::chrono::microseconds interval, Callable&& f, bool start_immediately = true) {
            return _call_every(interval, std::forward<Callable>(f), unevent_loop::loop_id, start_immediately);
        }

        template <std::invocable Callable>
        void call_later(std::chrono::microseconds delay, Callable hook) {
            if (in_event_loop()) {
                add_oneshot_event(delay, std::move(hook));
            }
            else {
                call_soon([this, func = std::move(hook), target_time = detail::get_time() + delay]() mutable {
                    auto now = detail::get_time();

                    if (now >= target_time) {
                        func();
                    }
                    else {
                        add_oneshot_event(
                                std::chrono::duration_cast<std::chrono::microseconds>(target_time - now),
                                std::move(func));
                    }
                });
            }
        }

        template <std::invocable Callable>
        void call_soon(Callable f) {
            {
                std::lock_guard lock{job_queue_mutex};
                job_queue.emplace_back(std::move(f));
            }

            event_active(job_waker.get(), 0, 0);
        }

        bool in_event_loop() const noexcept { return std::this_thread::get_id() == loop_thread_id; }

        // Similar in concept to std::make_shared<T>, but it creates the shared pointer with a
        // custom deleter that dispatches actual object destruction to the network's event loop for
        // thread safety.
        template <typename T, typename... Args>
        std::shared_ptr<T> make_shared(Args&&... args) {
            auto* ptr = new T{std::forward<Args>(args)...};
            return std::shared_ptr<T>{ptr, loop_deleter<T>()};
        }

        // Similar to the above make_shared, but instead of forwarding arguments for the
        // construction of the object, it creates the shared_ptr from the already created object ptr
        // and wraps the object's deleter in a wrapped_deleter
        template <typename T, std::invocable<T*> Callable>
        std::shared_ptr<T> shared_ptr(T* obj, Callable&& deleter) {
            return std::shared_ptr<T>(obj, wrapped_deleter<T>(std::forward<Callable>(deleter)));
        }

        // Returns a pointer deleter that defers the actual destruction call to this network
        // object's event loop.
        template <typename T>
        auto loop_deleter() {
            auto weak_self = this->weak_from_this();
            return [weak_self](T* ptr) {
                if (auto self = weak_self.lock()) {
                    self->call_get([ptr] { delete ptr; });
                }
                else {
                    delete ptr;
                }
            };
        }

        // Returns a pointer deleter that defers invocation of a custom deleter to the event loop
        template <typename T, std::invocable<T*> Callable>
        auto wrapped_deleter(Callable f) {
            auto weak_self = this->weak_from_this();
            return [weak_self, func = std::move(f)](T* ptr) mutable {
                if (auto self = weak_self.lock()) {
                    self->call_get([f = std::move(func), ptr]() mutable { f(ptr); });
                }
                else {
                    func(ptr);
                }
            };
        }

        void stop_thread() {
            unlog::debug(log, "Stopping loop thread...");

            event_base_loopbreak(ev_loop.get());

            if (loop_thread.joinable()) {
                in_event_loop() ? loop_thread.detach() : loop_thread.join();
            }
        }

        void stop_tickers(caller_id_t id) {
            if (auto it = tickers.find(id); it != tickers.end()) {
                for (auto& t : it->second) {
                    if (auto tick = t.lock()) {
                        tick->f = nullptr;
                        tick->stop();
                    }
                }
            }
        }

      private:
        template <std::invocable Callable>
        void add_oneshot_event(std::chrono::microseconds delay, Callable hook) {
            auto handler = make_handler(unevent_loop::loop_id);
            auto& h = *handler;

            h.init_event(
                    loop(),
                    delay,
                    [hndlr = std::move(handler), func = std::move(hook)]() mutable {
                        auto h = std::move(hndlr);
                        func();
                        h.reset();
                    },
                    true);
        }

        void shutdown() {
            unlog::trace(log, "{} called", __PRETTY_FUNCTION__);

            for (auto& [id, list] : tickers) {
                std::ranges::for_each(list, [](auto&& t) {
                    if (auto tick = t.lock()) {
                        tick->f = nullptr;
                        tick->stop();
                    }
                });
            }

            running.store(false, std::memory_order_release);
        }

        void clear_old_tickers() {
            for (auto& [id, list] : tickers) {
                for (auto itr = list.begin(); itr != list.end();) {
                    if (itr->expired()) {
                        itr = list.erase(itr);
                    }
                    else {
                        ++itr;
                    }
                }
            }
        }

        std::shared_ptr<ev_watcher> make_handler(caller_id_t _id) {
            clear_old_tickers();
            auto t = make_shared<unevent_loop::ev_watcher>();
            tickers[_id].push_back(t);
            return t;
        }

        static constexpr caller_id_t loop_id{0};

        template <typename Callable>
        [[nodiscard]] std::shared_ptr<ev_watcher> _call_every(
                std::chrono::microseconds interval, Callable&& f, caller_id_t _id, bool start_immediately) {
            auto h = make_handler(_id);

            h->init_event(loop(), interval, std::forward<Callable>(f), false, start_immediately);

            return h;
        }

        void setup_job_waker() {
            job_waker.reset(event_new(
                    ev_loop.get(),
                    -1,
                    0,
                    [](evutil_socket_t, short, void* self) {
                        unlog::trace(log, "processing job queue");
                        static_cast<unevent_loop*>(self)->process_job_queue();
                    },
                    this));
            assert(job_waker);
        }

        void process_job_queue() {
            unlog::trace(log, "Event loop processing job queue");
            assert(in_event_loop());

            decltype(job_queue) swapped_queue;

            {
                std::lock_guard<std::mutex> lock{job_queue_mutex};
                job_queue.swap(swapped_queue);
            }

            while (not swapped_queue.empty() && running.load(std::memory_order_acquire)) {
                try {
                    auto front = std::move(swapped_queue.front());
                    static_assert(std::same_as<std::decay_t<decltype(front)>, job_hook>);
                    swapped_queue.pop_front();
                    front();
                } catch (const std::exception& e) {
                    unlog::critical(log, "Queued job threw exception: {}", e.what());
                } catch (...) {
                    unlog::critical(log, "Queued job threw non-std exception");
                }
            }
        }
        friend struct test::test_helper;
    };
}  // namespace un::event
