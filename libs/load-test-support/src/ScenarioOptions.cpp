#include "rss/tools/ScenarioOptions.h"

#include <charconv>
#include <stdexcept>
#include <string>

#include "rss/protocol/Packet.h"

namespace rss::tools {
namespace {

std::size_t parseNumber(std::string_view value, std::string_view option,
                        bool allow_zero = false) {
  std::size_t result{};
  const auto [position, error] =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (error != std::errc{} || position != value.data() + value.size() ||
      (!allow_zero && result == 0)) {
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
      options.clients = parseNumber(value, option);
    } else if (option == "--rooms") {
      options.rooms = parseNumber(value, option);
    } else if (option == "--messages") {
      options.messages_per_sender = parseNumber(value, option);
    } else if (option == "--payload-bytes") {
      options.payload_bytes = parseNumber(value, option);
    } else if (option == "--slow-clients") {
      options.slow_clients = parseNumber(value, option);
    } else if (option == "--repeat") {
      options.repeats = parseNumber(value, option);
    } else if (option == "--workers") {
      options.worker_count = parseNumber(value, option);
    } else if (option == "--rate-per-client") {
      options.rate_per_client = parseNumber(value, option, true);
    } else if (option == "--max-in-flight") {
      options.max_in_flight = parseNumber(value, option, true);
    } else if (option == "--timeout-seconds") {
      options.timeout_seconds = parseNumber(value, option);
    } else {
      throw std::invalid_argument("unknown option: " + std::string{option});
    }
  }

  validateScenarioOptions(options);
  return options;
}

void validateScenarioOptions(const ScenarioOptions& options) {
  if (options.clients == 0 || options.clients > 1000 ||
      options.worker_count == 0 || options.worker_count > 1000) {
    throw std::invalid_argument(
        "clients and workers must be between 1 and 1000");
  }
  if (options.messages_per_sender == 0 || options.rooms == 0 ||
      options.repeats == 0) {
    throw std::invalid_argument("messages, rooms and repeat must be positive");
  }
  if (options.rate_per_client > 1000000 || options.timeout_seconds == 0 ||
      options.timeout_seconds > 3600) {
    throw std::invalid_argument(
        "rate per client must be 0..1000000 and timeout seconds 1..3600");
  }
  if (options.payload_bytes < 64 ||
      options.payload_bytes > protocol::kMaxChatMessageBytes) {
    throw std::invalid_argument("payload bytes must be between 64 and " +
                                std::to_string(protocol::kMaxChatMessageBytes));
  }
  if (options.scenario == ScenarioKind::MultiRoom &&
      options.rooms > options.clients) {
    throw std::invalid_argument("rooms cannot exceed clients");
  }
  if (options.scenario == ScenarioKind::SlowClient &&
      (options.slow_clients == 0 || options.slow_clients >= options.clients)) {
    throw std::invalid_argument("slow clients must be fewer than clients");
  }
  // 방마다 모든 빠른 송신자의 메시지를 모든 빠른 수신자가 보관한다.
  // 클라이언트 상한 덕분에 방 크기의 제곱 합은 size_t 범위 안이다.
  const auto fast_clients = options.scenario == ScenarioKind::SlowClient
                                ? options.clients - options.slow_clients
                                : options.clients;
  const auto rooms =
      options.scenario == ScenarioKind::MultiRoom ? options.rooms : 1;
  const auto small_room = fast_clients / rooms;
  const auto large_rooms = fast_clients % rooms;
  const auto receipts_per_sequence =
      (rooms - large_rooms) * small_room * small_room +
      large_rooms * (small_room + 1) * (small_room + 1);
  constexpr std::size_t kMaxReceiptSamples = 5000000;
  if (options.messages_per_sender >
      kMaxReceiptSamples / receipts_per_sequence) {
    throw std::invalid_argument(
        "expected broadcast samples cannot exceed 5000000");
  }
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
