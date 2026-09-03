#pragma once

#include <chrono>
#include <string_view>

namespace rss::observability {

[[nodiscard]] std::chrono::milliseconds parseReportingIntervalSeconds(
    std::string_view value);

}  // namespace rss::observability
