#pragma once

#include <chrono>
#include <string_view>

namespace rss::observability {

[[nodiscard]] std::chrono::seconds parseReportingIntervalSeconds(
    std::string_view value);

}  // namespace rss::observability
