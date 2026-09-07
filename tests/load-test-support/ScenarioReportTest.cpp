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
  options.rate_per_client = 25;
  options.max_in_flight = 4;
  options.timeout_seconds = 90;

  rss::tools::ScenarioRunResult result;
  result.requested = options;
  result.effective_rooms = 1;
  result.effective_max_in_flight = 4;
  result.effective_timeout_ms = 90000;
  result.sent = 2;
  result.expected_broadcasts = 4;
  result.received_broadcasts = 4;
  result.latencies = {
      std::chrono::milliseconds{1}, std::chrono::milliseconds{2},
      std::chrono::milliseconds{3}, std::chrono::milliseconds{4}};
  result.elapsed = std::chrono::seconds{2};
  result.client_failures = {
      .setup = {1, 2, 3, 4, 5},
      .send = {6, 7, 8, 9, 10},
      .receive = {11, 12, 13, 14, 15},
  };
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
      .disconnect_peer_closed = 14,
      .disconnect_socket_error = 15,
      .disconnect_protocol_error = 16,
      .disconnect_idle_timeout = 17,
      .disconnect_worker_requested = 18,
      .disconnect_pending_write_limit = 19,
      .disconnect_close_after_flush = 20,
      .disconnect_shutdown = 21,
      .worker_parked_limit_failures = 22,
      .worker_invalid_sequence_failures = 23,
      .worker_deferred_failures = 24,
  };

  EXPECT_EQ(
      rss::tools::formatRunResult(2, options.scenario, result),
      "run=2 scenario=slow-client clients=3 rooms=1 "
      "messages_per_sender=11 payload_bytes=512 slow_clients=1 repeats=4 "
      "rate_per_client=25 max_in_flight=4 effective_max_in_flight=4 "
      "timeout_seconds=90 effective_timeout_ms=90000 "
      "sent=2 expected=4 received=4 missing=0 duplicates=0 unexpected=0 "
      "failed_clients=0 "
      "client_setup_peer_closed=1 client_setup_socket_error=2 "
      "client_setup_timeout=3 client_setup_protocol=4 client_setup_other=5 "
      "client_send_peer_closed=6 client_send_socket_error=7 "
      "client_send_timeout=8 client_send_protocol=9 client_send_other=10 "
      "client_receive_peer_closed=11 client_receive_socket_error=12 "
      "client_receive_timeout=13 client_receive_protocol=14 "
      "client_receive_other=15 elapsed_sec=2.000 "
      "throughput_broadcasts_per_sec=2.000 p50_ms=2.000 p95_ms=4.000 "
      "p99_ms=4.000 read_pauses=5 inbound_queue_full=6 "
      "outbound_budget_rejections=7 handler_exceptions=8 "
      "slow_client_disconnects=9 rejected_connections=10 "
      "max_inbound_queue_size=11 max_outbound_queue_size=12 "
      "max_session_pending_write_bytes=13 "
      "disconnect_peer_closed=14 "
      "disconnect_socket_error=15 "
      "disconnect_protocol_error=16 "
      "disconnect_idle_timeout=17 "
      "disconnect_worker_requested=18 "
      "disconnect_pending_write_limit=19 "
      "disconnect_close_after_flush=20 "
      "disconnect_shutdown=21 "
      "worker_parked_limit_failures=22 "
      "worker_invalid_sequence_failures=23 "
      "worker_deferred_failures=24");
}

}  // namespace
