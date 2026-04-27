#pragma once

#include <cstdint>
#include <string>

namespace rss::domain {

struct User {
    std::uint64_t id{};
    std::uint64_t session_id{};
    std::string name;
};

} // namespace rss::domain
