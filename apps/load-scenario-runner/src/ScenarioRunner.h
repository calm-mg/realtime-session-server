#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "rss/tools/ScenarioOptions.h"
#include "rss/tools/ScenarioReport.h"

namespace rss::tools {

struct MessageIdentity {
  std::size_t run{};
  std::size_t sender{};
  std::size_t sequence{};
  std::uint64_t sent_us{};
};

std::string makeScenarioPayload(std::size_t run, std::size_t sender,
                                std::size_t sequence, std::uint64_t sent_us,
                                std::size_t payload_bytes);
MessageIdentity parseScenarioPayload(std::string_view payload);

class ScenarioRunner {
 public:
  ScenarioRunner() = default;
  explicit ScenarioRunner(std::chrono::milliseconds scenario_timeout);

  ScenarioRunResult runOnce(const ScenarioOptions& options,
                            std::size_t run_id) const;

 private:
  std::chrono::milliseconds scenario_timeout_{std::chrono::seconds{30}};
};

}  // namespace rss::tools
