#include "rss/tools/ScenarioReport.h"

#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "rss/tools/LatencyStats.h"

namespace rss::tools {

std::uint64_t expectedBroadcasts(
    std::span<const std::size_t> room_sizes,
    std::span<const std::uint64_t> successful_sends_by_room) {
  if (room_sizes.size() != successful_sends_by_room.size()) {
    throw std::invalid_argument(
        "room sizes and successful send counts must have equal lengths");
  }

  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t broadcasts{};
  for (std::size_t index = 0; index < room_sizes.size(); ++index) {
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
      if (room_sizes[index] > maximum) {
        throw std::overflow_error("scenario room size overflow");
      }
    }
    const auto room_size = static_cast<std::uint64_t>(room_sizes[index]);
    const auto successful_sends = successful_sends_by_room[index];
    if (room_size != 0 && successful_sends > maximum / room_size) {
      throw std::overflow_error("scenario expected broadcast overflow");
    }
    const auto room_broadcasts = room_size * successful_sends;
    if (room_broadcasts > maximum - broadcasts) {
      throw std::overflow_error("scenario expected broadcast overflow");
    }
    broadcasts += room_broadcasts;
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
                            const ScenarioRunResult& result) {
  const auto latency = latencyReport(result.latencies);
  const auto elapsed_seconds = result.elapsed.count() / 1'000'000.0;
  const auto throughput = elapsed_seconds > 0.0
                              ? result.received_broadcasts / elapsed_seconds
                              : 0.0;

  std::ostringstream output;
  const auto& options = result.requested;
  output << std::fixed << std::setprecision(3) << "run=" << run
         << " scenario=" << scenarioName(kind) << " clients=" << options.clients
         << " rooms=" << result.effective_rooms
         << " messages_per_sender=" << options.messages_per_sender
         << " payload_bytes=" << options.payload_bytes
         << " slow_clients=" << options.slow_clients
         << " repeats=" << options.repeats << " sent=" << result.sent
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
