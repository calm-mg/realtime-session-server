#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "rss/tools/ScenarioOptions.h"

namespace rss::tools {

struct OverloadReport {
  std::uint64_t read_pauses{};
  std::uint64_t read_resumes{};
  std::uint64_t inbound_queue_full{};
  std::uint64_t outbound_budget_rejections{};
  std::uint64_t slow_client_disconnects{};
  std::uint64_t rejected_connections{};
  std::size_t max_inbound_queue_size{};
  std::size_t max_outbound_queue_size{};
  std::size_t max_session_pending_write_bytes{};
};

struct ScenarioRunResult {
  std::uint64_t sent{};
  std::uint64_t expected_broadcasts{};
  std::uint64_t received_broadcasts{};
  std::uint64_t missing_broadcasts{};
  std::uint64_t duplicate_broadcasts{};
  std::uint64_t unexpected_broadcasts{};
  std::uint64_t failed_clients{};
  std::vector<std::chrono::microseconds> latencies;
  OverloadReport overload;
  std::chrono::microseconds elapsed{};
};

std::uint64_t expectedBroadcasts(std::span<const std::size_t> room_sizes,
                                 std::size_t messages_per_sender);
bool isSuccessful(ScenarioKind kind, const ScenarioRunResult& result,
                  std::size_t required_slow_disconnects) noexcept;
std::string formatRunResult(std::size_t run, ScenarioKind kind,
                            const ScenarioOptions& options,
                            const ScenarioRunResult& result);

}  // namespace rss::tools
