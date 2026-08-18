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

struct ScenarioTuning {
  std::size_t max_pending_write_bytes{1024U * 1024U};
  int socket_receive_buffer_bytes{1024};
  std::chrono::milliseconds scenario_timeout{std::chrono::seconds{30}};
};

std::string makeScenarioPayload(std::size_t run, std::size_t sender,
                                std::size_t sequence, std::uint64_t sent_us,
                                std::size_t payload_bytes);
MessageIdentity parseScenarioPayload(std::string_view payload);

class ScenarioRunner {
 public:
  explicit ScenarioRunner(ScenarioTuning tuning = {});

  ScenarioRunResult runOnce(const ScenarioOptions& options,
                            std::size_t run_id) const;

 private:
  ScenarioTuning tuning_;
};

}  // namespace rss::tools
