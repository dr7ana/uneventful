#include "utils.hpp"

namespace un::event::test {

    channel_type test_channel = [] {
        auto channel = test_log::make_channel(channel_config::make("unevent"));
        test_log::start();
        return channel;
    }();

}  // namespace un::event::test
