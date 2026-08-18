#include "rss/tools/ScenarioReport.h"

#include <iomanip>
#include <sstream>

#include "rss/tools/LatencyStats.h"

namespace rss::tools {

std::uint64_t expectedBroadcasts(std::span<const std::size_t> room_sizes,
                                 std::size_t messages_per_sender) {
  std::uint64_t broadcasts{};
  for (const auto room_size : room_sizes) {
    broadcasts += static_cast<std::uint64_t>(room_size) *
                  static_cast<std::uint64_t>(messages_per_sender) *
                  static_cast<std::uint64_t>(room_size);
  }
  return broadcasts;
}

bool isSuccessful(ScenarioKind kind, const ScenarioRunResult& result,
                  std::size_t required_slow_disconnects) noexcept {
  if (result.expected_broadcasts != result.received_broadcasts ||
      result.missing_broadcasts != 0 || result.duplicate_broadcasts != 0 ||
      result.unexpected_broadcasts != 0 || result.failed_clients != 0) {
    return false;
  }
  return kind != ScenarioKind::SlowClient ||
         result.overload.slow_client_disconnects >= required_slow_disconnects;
}

std::string formatRunResult(std::size_t run, ScenarioKind kind,
                            const ScenarioOptions& options,
                            const ScenarioRunResult& result) {
  const auto latency = latencyReport(result.latencies);
  const auto elapsed_seconds = result.elapsed.count() / 1'000'000.0;
  const auto throughput = elapsed_seconds > 0.0
                              ? result.received_broadcasts / elapsed_seconds
                              : 0.0;

  std::ostringstream output;
  output << std::fixed << std::setprecision(3) << "run=" << run
         << " scenario=" << scenarioName(kind) << " clients=" << options.clients
         << " rooms=" << options.rooms << " sent=" << result.sent
         << " expected=" << result.expected_broadcasts
         << " received=" << result.received_broadcasts
         << " missing=" << result.missing_broadcasts
         << " duplicates=" << result.duplicate_broadcasts
         << " unexpected=" << result.unexpected_broadcasts
         << " failed_clients=" << result.failed_clients
         << " elapsed_sec=" << elapsed_seconds
         << " throughput_broadcasts_per_sec=" << throughput
         << " p50_ms=" << latency.p50_us / 1000.0
         << " p95_ms=" << latency.p95_us / 1000.0
         << " p99_ms=" << latency.p99_us / 1000.0
         << " read_pauses=" << result.overload.read_pauses
         << " inbound_queue_full=" << result.overload.inbound_queue_full
         << " outbound_budget_rejections="
         << result.overload.outbound_budget_rejections
         << " slow_client_disconnects="
         << result.overload.slow_client_disconnects
         << " rejected_connections=" << result.overload.rejected_connections
         << " max_inbound_queue_size=" << result.overload.max_inbound_queue_size
         << " max_outbound_queue_size="
         << result.overload.max_outbound_queue_size
         << " max_session_pending_write_bytes="
         << result.overload.max_session_pending_write_bytes;
  return output.str();
}

}  // namespace rss::tools
