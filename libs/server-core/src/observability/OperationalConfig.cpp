#include "rss/observability/OperationalConfig.h"

#include <charconv>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace rss::observability {

std::chrono::seconds parseReportingIntervalSeconds(std::string_view value) {
  std::uint64_t parsed{};
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (value.empty() || error != std::errc{} ||
      end != value.data() + value.size() ||
      parsed > static_cast<std::uint64_t>(
                   std::numeric_limits<std::chrono::seconds::rep>::max())) {
    throw std::invalid_argument(
        "RSS_OBSERVABILITY_INTERVAL_SECONDS must be a non-negative integer");
  }
  return std::chrono::seconds(parsed);
}

}  // namespace rss::observability
