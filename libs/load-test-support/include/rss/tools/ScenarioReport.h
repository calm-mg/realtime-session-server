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
  std::uint64_t handler_exceptions{};
  std::uint64_t slow_client_disconnects{};
  std::uint64_t rejected_connections{};
  std::size_t max_inbound_queue_size{};
  std::size_t max_outbound_queue_size{};
  std::size_t max_session_pending_write_bytes{};
  std::uint64_t disconnect_peer_closed{};
  std::uint64_t disconnect_socket_error{};
  std::uint64_t disconnect_protocol_error{};
  std::uint64_t disconnect_idle_timeout{};
  std::uint64_t disconnect_worker_requested{};
  std::uint64_t disconnect_pending_write_limit{};
  std::uint64_t disconnect_close_after_flush{};
  std::uint64_t disconnect_shutdown{};
  std::uint64_t worker_parked_limit_failures{};
  std::uint64_t worker_invalid_sequence_failures{};
  std::uint64_t worker_deferred_failures{};
};

struct ClientFailureCounts {
  std::uint64_t peer_closed{};
  std::uint64_t socket_error{};
  std::uint64_t timeout{};
  std::uint64_t protocol{};
  std::uint64_t other{};
};

struct ClientFailureReport {
  // 관측한 단계별 실패다. 같은 클라이언트가 send와 receive에 모두 포함될 수
  // 있다. setup 중단 후 미시도 클라이언트는 failed_clients에만 포함한다.
  ClientFailureCounts setup;
  ClientFailureCounts send;
  ClientFailureCounts receive;
};

struct ScenarioRunResult {
  ScenarioOptions requested;
  std::size_t effective_rooms{1};
  std::size_t effective_max_in_flight{};
  std::uint64_t effective_timeout_ms{};
  std::uint64_t sent{};
  std::uint64_t expected_broadcasts{};
  std::uint64_t received_broadcasts{};
  std::uint64_t missing_broadcasts{};
  std::uint64_t duplicate_broadcasts{};
  std::uint64_t unexpected_broadcasts{};
  std::uint64_t failed_clients{};
  ClientFailureReport client_failures;
  std::vector<std::chrono::microseconds> latencies;
  bool server_stats_available{true};
  OverloadReport overload;
  std::chrono::microseconds elapsed{};
};

std::uint64_t expectedBroadcasts(
    std::span<const std::size_t> room_sizes,
    std::span<const std::uint64_t> successful_sends_by_room);
bool isSuccessful(ScenarioKind kind, const ScenarioRunResult& result,
                  std::size_t required_slow_disconnects) noexcept;
std::string formatRunResult(std::size_t run, ScenarioKind kind,
                            const ScenarioRunResult& result);

}  // namespace rss::tools
