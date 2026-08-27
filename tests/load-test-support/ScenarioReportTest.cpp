#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <limits>
#include <stdexcept>

#include "rss/tools/ScenarioReport.h"

namespace {

TEST(ScenarioReportTest, CalculatesExpectedBroadcastsAcrossRooms) {
  const std::array<std::size_t, 2> room_sizes{3, 2};
  const std::array<std::uint64_t, 2> successful_sends{12, 8};

  EXPECT_EQ(rss::tools::expectedBroadcasts(room_sizes, successful_sends), 52U);
}

TEST(ScenarioReportTest, RejectsExpectedBroadcastOverflow) {
  const std::array<std::size_t, 1> room_sizes{2};
  const std::array<std::uint64_t, 1> successful_sends{
      std::numeric_limits<std::uint64_t>::max()};

  EXPECT_THROW(static_cast<void>(rss::tools::expectedBroadcasts(
                   room_sizes, successful_sends)),
               std::overflow_error);
}

TEST(ScenarioReportTest, FailsWhenAnyObservableErrorExists) {
  rss::tools::ScenarioRunResult result;
  result.expected_broadcasts = 10;
  result.received_broadcasts = 9;
  result.missing_broadcasts = 1;

  EXPECT_FALSE(
      rss::tools::isSuccessful(rss::tools::ScenarioKind::Broadcast, result, 0));
}

TEST(ScenarioReportTest, RequiresConfiguredSlowClientDisconnects) {
  rss::tools::ScenarioRunResult result;
  result.overload.slow_client_disconnects = 1;

  EXPECT_FALSE(rss::tools::isSuccessful(rss::tools::ScenarioKind::SlowClient,
                                        result, 2));

  result.overload.slow_client_disconnects = 2;

  EXPECT_TRUE(rss::tools::isSuccessful(rss::tools::ScenarioKind::SlowClient,
                                       result, 2));
}

TEST(ScenarioReportTest, FormatsEveryReproducibilityInputInStableOrder) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::SlowClient;
  options.clients = 3;
  options.rooms = 7;
  options.messages_per_sender = 11;
  options.payload_bytes = 512;
  options.slow_clients = 1;
  options.repeats = 4;

  rss::tools::ScenarioRunResult result;
  result.requested = options;
  result.effective_rooms = 1;
  result.sent = 2;
  result.expected_broadcasts = 4;
  result.received_broadcasts = 4;
  result.latencies = {
      std::chrono::milliseconds{1}, std::chrono::milliseconds{2},
      std::chrono::milliseconds{3}, std::chrono::milliseconds{4}};
  result.elapsed = std::chrono::seconds{2};
  result.overload = {
      .read_pauses = 5,
      .inbound_queue_full = 6,
      .outbound_budget_rejections = 7,
      .handler_exceptions = 8,
      .slow_client_disconnects = 9,
      .rejected_connections = 10,
      .max_inbound_queue_size = 11,
      .max_outbound_queue_size = 12,
      .max_session_pending_write_bytes = 13,
  };

  EXPECT_EQ(rss::tools::formatRunResult(2, options.scenario, result),
            "run=2 scenario=slow-client clients=3 rooms=1 "
            "messages_per_sender=11 payload_bytes=512 slow_clients=1 repeats=4 "
            "sent=2 expected=4 received=4 missing=0 duplicates=0 unexpected=0 "
            "failed_clients=0 elapsed_sec=2.000 "
            "throughput_broadcasts_per_sec=2.000 p50_ms=2.000 p95_ms=4.000 "
            "p99_ms=4.000 read_pauses=5 inbound_queue_full=6 "
            "outbound_budget_rejections=7 handler_exceptions=8 "
            "slow_client_disconnects=9 rejected_connections=10 "
            "max_inbound_queue_size=11 max_outbound_queue_size=12 "
            "max_session_pending_write_bytes=13");
}

}  // namespace
