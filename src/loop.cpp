#include "uneventful/loop.hpp"

#include <unlog/config.hpp>

#ifdef UNEVENTFUL_SSL_ENABLED
extern "C" {
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/types.h>
}
#endif

namespace un::event {

    namespace detail {
        void setup_ssl_library() {
#ifdef UNEVENTFUL_SSL_ENABLED
            OPENSSL_init_ssl(0, NULL);
            SSL_load_error_strings();
            OpenSSL_add_all_algorithms();
            OpenSSL_add_all_ciphers();
#endif
        }
    }  // namespace detail

    namespace detail {
        static std::vector<std::string_view> get_ev_methods() {
            std::vector<std::string_view> ev_methods_avail;
            for (const char** methods = event_get_supported_methods(); methods && *methods; methods++) {
                ev_methods_avail.emplace_back(*methods);
            }
            return ev_methods_avail;
        }

        struct event_base* try_make_et_evbase() {
            if (static bool once = false; !once) {
                once = true;
                detail::setup_ssl_library();

                evthread_use_pthreads();
            }

            static std::array<int, 2> features{EV_FEATURE_ET, 0};
            static std::vector<std::string_view> ev_methods_avail = get_ev_methods();

            std::unique_ptr<event_config, decltype(&event_config_free)> ev_conf{event_config_new(), event_config_free};
            event_config_set_flag(ev_conf.get(), EVENT_BASE_FLAG_PRECISE_TIMER);
            event_config_set_flag(ev_conf.get(), EVENT_BASE_FLAG_NO_CACHE_TIME);
            event_config_set_flag(ev_conf.get(), EVENT_BASE_FLAG_EPOLL_USE_CHANGELIST);

            for (auto& feature : features) {
                event_config_require_features(ev_conf.get(), feature);

                if (auto base = event_base_new_with_config(ev_conf.get())) {
                    return base;
                }
            }

            throw std::runtime_error{"Failed to create edge-triggered or standard I/O event base!"};
        }
    }  // namespace detail

}  // namespace un::event
