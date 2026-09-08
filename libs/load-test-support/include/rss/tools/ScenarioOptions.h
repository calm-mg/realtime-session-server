#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace rss::tools {

enum class ScenarioKind { Broadcast, MultiRoom, SlowClient };

struct ScenarioOptions {
  std::string host{};
  std::uint16_t port{};
  ScenarioKind scenario{ScenarioKind::Broadcast};
  std::size_t clients{10};
  std::size_t rooms{2};
  std::size_t messages_per_sender{100};
  std::size_t payload_bytes{256};
  std::size_t slow_clients{1};
  std::size_t repeats{5};
  std::size_t worker_count{4};
  std::size_t rate_per_client{0};
  std::size_t max_in_flight{0};
  std::size_t timeout_seconds{30};
};

void validateScenarioOptions(const ScenarioOptions& options);
ScenarioOptions parseScenarioOptions(std::span<const std::string_view> args);
std::string_view scenarioName(ScenarioKind kind) noexcept;

}  // namespace rss::tools
