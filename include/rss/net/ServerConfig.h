#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace rss::net {

struct ServerConfig {
    std::string host{"0.0.0.0"};
    std::uint16_t port{7777};
    std::size_t worker_count{4};
    int backlog{512};
    int max_events{256};
    std::chrono::seconds idle_timeout{60};
};

} // namespace rss::net
