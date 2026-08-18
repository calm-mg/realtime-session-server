#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace rss::tools {

enum class ScenarioKind { Broadcast, MultiRoom, SlowClient };

struct ScenarioOptions {
  ScenarioKind scenario{ScenarioKind::Broadcast};
  std::size_t clients{10};
  std::size_t rooms{2};
  std::size_t messages_per_sender{100};
  std::size_t payload_bytes{256};
  std::size_t slow_clients{1};
  std::size_t repeats{5};
  std::size_t worker_count{4};
};

ScenarioOptions parseScenarioOptions(std::span<const std::string_view> args);
std::string_view scenarioName(ScenarioKind kind) noexcept;

}  // namespace rss::tools
