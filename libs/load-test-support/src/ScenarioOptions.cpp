#include "rss/tools/ScenarioOptions.h"

#include <charconv>
#include <stdexcept>
#include <string>

namespace rss::tools {
namespace {

std::size_t parsePositive(std::string_view value, std::string_view option) {
  std::size_t result{};
  const auto [position, error] =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (error != std::errc{} || position != value.data() + value.size() ||
      result == 0) {
    throw std::invalid_argument("invalid value for " + std::string{option});
  }
  return result;
}

ScenarioKind parseScenario(std::string_view value) {
  if (value == "broadcast") {
    return ScenarioKind::Broadcast;
  }
  if (value == "multi-room") {
    return ScenarioKind::MultiRoom;
  }
  if (value == "slow-client") {
    return ScenarioKind::SlowClient;
  }
  throw std::invalid_argument("invalid scenario");
}

}  // namespace

ScenarioOptions parseScenarioOptions(std::span<const std::string_view> args) {
  ScenarioOptions options;
  for (std::size_t index = 0; index < args.size(); index += 2) {
    if (index + 1 >= args.size()) {
      throw std::invalid_argument("missing option value");
    }

    const auto option = args[index];
    const auto value = args[index + 1];
    if (option == "--scenario") {
      options.scenario = parseScenario(value);
    } else if (option == "--clients") {
      options.clients = parsePositive(value, option);
    } else if (option == "--rooms") {
      options.rooms = parsePositive(value, option);
    } else if (option == "--messages") {
      options.messages_per_sender = parsePositive(value, option);
    } else if (option == "--payload-bytes") {
      options.payload_bytes = parsePositive(value, option);
    } else if (option == "--slow-clients") {
      options.slow_clients = parsePositive(value, option);
    } else if (option == "--repeat") {
      options.repeats = parsePositive(value, option);
    } else if (option == "--workers") {
      options.worker_count = parsePositive(value, option);
    } else {
      throw std::invalid_argument("unknown option: " + std::string{option});
    }
  }

  if (options.payload_bytes < 64 || options.payload_bytes > 4000) {
    throw std::invalid_argument("payload bytes must be between 64 and 4000");
  }
  if (options.scenario == ScenarioKind::MultiRoom &&
      options.rooms > options.clients) {
    throw std::invalid_argument("rooms cannot exceed clients");
  }
  if (options.scenario == ScenarioKind::SlowClient &&
      options.slow_clients >= options.clients) {
    throw std::invalid_argument("slow clients must be fewer than clients");
  }
  return options;
}

std::string_view scenarioName(ScenarioKind kind) noexcept {
  switch (kind) {
    case ScenarioKind::Broadcast:
      return "broadcast";
    case ScenarioKind::MultiRoom:
      return "multi-room";
    case ScenarioKind::SlowClient:
      return "slow-client";
  }
  return "unknown";
}

}  // namespace rss::tools
