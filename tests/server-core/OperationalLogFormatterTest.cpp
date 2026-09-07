#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>

#include "rss/observability/OperationalLogFormatter.h"

namespace {

using rss::net::OverloadSnapshot;
using rss::observability::SnapshotPhase;

TEST(OperationalLogFormatterTest, ReadsCurrentUnixTimeInMilliseconds) {
  const auto before = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const auto actual = rss::observability::currentUnixTimeMilliseconds();
  const auto after = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

  EXPECT_GE(actual, before);
  EXPECT_LE(actual, after);
}

TEST(OperationalLogFormatterTest, FormatsServerStartedAsOneJsonLine) {
  EXPECT_EQ(rss::observability::formatServerStarted(123, "127.0.0.1", 7777, 4),
            "{\"timestamp_unix_ms\":123,\"level\":\"info\","
            "\"event\":\"server_started\",\"host\":\"127.0.0.1\","
            "\"port\":7777,\"worker_count\":4}\n");
}

TEST(OperationalLogFormatterTest, EscapesJsonControlCharactersInFailure) {
  EXPECT_EQ(rss::observability::formatServerFailed(
                456, "database \"primary\" failed\nretry\tlater\\now"),
            "{\"timestamp_unix_ms\":456,\"level\":\"error\","
            "\"event\":\"server_failed\","
            "\"message\":\"database \\\"primary\\\" failed\\nretry\\tlater"
            "\\\\now\"}\n");
}

TEST(OperationalLogFormatterTest, FormatsEveryOverloadFieldInStableOrder) {
  const OverloadSnapshot snapshot{
      .read_pauses = 1,
      .read_resumes = 2,
      .inbound_queue_full = 3,
      .outbound_budget_rejections = 4,
      .handler_exceptions = 5,
      .slow_client_disconnects = 6,
      .rejected_connections = 7,
      .max_inbound_queue_size = 8,
      .max_outbound_queue_size = 9,
      .max_session_pending_write_bytes = 10,
      .current_inbound_queue_size = 11,
      .current_outbound_queue_size = 12,
      .outbound_queue_waiting_producers = 13,
      .current_sessions = 14,
      .outbound_queue_closed = true,
      .disconnect_peer_closed = 15,
      .disconnect_socket_error = 16,
      .disconnect_protocol_error = 17,
      .disconnect_idle_timeout = 18,
      .disconnect_worker_requested = 19,
      .disconnect_pending_write_limit = 20,
      .disconnect_close_after_flush = 21,
      .disconnect_shutdown = 22,
      .worker_parked_limit_failures = 23,
      .worker_invalid_sequence_failures = 24,
      .worker_deferred_failures = 25,
  };

  EXPECT_EQ(rss::observability::formatOverloadSnapshot(
                789, SnapshotPhase::Periodic, snapshot),
            "{\"timestamp_unix_ms\":789,\"level\":\"info\","
            "\"event\":\"overload_snapshot\",\"phase\":\"periodic\","
            "\"read_pauses\":1,\"read_resumes\":2,"
            "\"inbound_queue_full\":3,\"outbound_budget_rejections\":4,"
            "\"handler_exceptions\":5,\"slow_client_disconnects\":6,"
            "\"rejected_connections\":7,\"max_inbound_queue_size\":8,"
            "\"max_outbound_queue_size\":9,"
            "\"max_session_pending_write_bytes\":10,"
            "\"current_inbound_queue_size\":11,"
            "\"current_outbound_queue_size\":12,"
            "\"outbound_queue_waiting_producers\":13,"
            "\"current_sessions\":14,\"outbound_queue_closed\":true"
            ",\"disconnect_peer_closed\":15"
            ",\"disconnect_socket_error\":16"
            ",\"disconnect_protocol_error\":17"
            ",\"disconnect_idle_timeout\":18"
            ",\"disconnect_worker_requested\":19"
            ",\"disconnect_pending_write_limit\":20"
            ",\"disconnect_close_after_flush\":21"
            ",\"disconnect_shutdown\":22"
            ",\"worker_parked_limit_failures\":23"
            ",\"worker_invalid_sequence_failures\":24"
            ",\"worker_deferred_failures\":25"
            "}\n");
}

TEST(OperationalLogFormatterTest, EmitsDisconnectAndWorkerFailureCounters) {
  const auto output = rss::observability::formatOverloadSnapshot(
      999, SnapshotPhase::Final, OverloadSnapshot{});
  for (const auto* field :
       {"disconnect_peer_closed", "disconnect_socket_error",
        "disconnect_protocol_error", "disconnect_idle_timeout",
        "disconnect_worker_requested", "disconnect_pending_write_limit",
        "disconnect_close_after_flush", "disconnect_shutdown",
        "worker_parked_limit_failures", "worker_invalid_sequence_failures",
        "worker_deferred_failures"}) {
    EXPECT_NE(output.find(std::string("\"") + field + "\":0"),
              std::string::npos)
        << field;
  }
}

TEST(OperationalLogFormatterTest, FormatsFinalSnapshotPhase) {
  const auto output = rss::observability::formatOverloadSnapshot(
      999, SnapshotPhase::Final, OverloadSnapshot{});

  EXPECT_NE(output.find("\"phase\":\"final\""), std::string::npos);
}

TEST(OperationalLogFormatterTest, FormatsServerStoppedAsOneJsonLine) {
  EXPECT_EQ(rss::observability::formatServerStopped(1000),
            "{\"timestamp_unix_ms\":1000,\"level\":\"info\","
            "\"event\":\"server_stopped\"}\n");
}

}  // namespace
