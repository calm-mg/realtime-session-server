#include "rss/observability/OperationalConfig.h"

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <system_error>

namespace rss::observability {

std::chrono::milliseconds parseReportingIntervalSeconds(
    std::string_view value) {
  std::uint64_t parsed{};
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (value.empty() || error != std::errc{} ||
      end != value.data() + value.size() ||
      parsed > static_cast<std::uint64_t>(
                   std::chrono::milliseconds::max().count() / 1000)) {
    throw std::invalid_argument(
        "RSS_OBSERVABILITY_INTERVAL_SECONDS must be a non-negative integer");
  }
  return std::chrono::milliseconds(parsed * 1000);
}

}  // namespace rss::observability
