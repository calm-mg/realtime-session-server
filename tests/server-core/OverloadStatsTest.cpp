#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <thread>
#include <vector>

#include "rss/net/OverloadStats.h"

namespace {

using rss::net::OverloadStats;

TEST(OverloadStatsTest, AccumulatesCountersAndKeepsObservedMaximums) {
  OverloadStats stats;

  for (int repeat = 0; repeat < 2; ++repeat) {
    stats.recordReadPause();
    stats.recordReadResume();
    stats.recordInboundQueueFull();
    stats.recordSlowClientDisconnect();
    stats.recordRejectedConnection();
  }
  for (const std::size_t size : std::array<std::size_t, 3>{2, 5, 3}) {
    stats.observeInboundQueueSize(size);
    stats.observeOutboundQueueSize(size);
    stats.observeSessionPendingWriteBytes(size);
  }

  const auto snapshot = stats.snapshot(7, 11, 13);

  EXPECT_EQ(snapshot.read_pauses, 2U);
  EXPECT_EQ(snapshot.read_resumes, 2U);
  EXPECT_EQ(snapshot.inbound_queue_full, 2U);
  EXPECT_EQ(snapshot.slow_client_disconnects, 2U);
  EXPECT_EQ(snapshot.rejected_connections, 2U);
  EXPECT_EQ(snapshot.max_inbound_queue_size, 5U);
  EXPECT_EQ(snapshot.max_outbound_queue_size, 5U);
  EXPECT_EQ(snapshot.max_session_pending_write_bytes, 5U);
  EXPECT_EQ(snapshot.current_inbound_queue_size, 7U);
  EXPECT_EQ(snapshot.current_outbound_queue_size, 11U);
  EXPECT_EQ(snapshot.current_sessions, 13U);
}

TEST(OverloadStatsTest, ConcurrentObservationsNeverReduceMaximums) {
  OverloadStats stats;
  constexpr std::size_t expected_maximum = 1000;
  constexpr std::size_t thread_count = 8;
  std::vector<std::thread> threads;
  threads.reserve(thread_count);

  for (std::size_t thread_index = 0; thread_index < thread_count;
       ++thread_index) {
    threads.emplace_back([&stats, thread_index] {
      for (std::size_t size = thread_index + 1; size <= expected_maximum;
           size += thread_count) {
        stats.observeInboundQueueSize(size);
        stats.observeOutboundQueueSize(size);
        stats.observeSessionPendingWriteBytes(size);
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  stats.observeInboundQueueSize(1);
  stats.observeOutboundQueueSize(1);
  stats.observeSessionPendingWriteBytes(1);
  const auto snapshot = stats.snapshot(0, 0, 0);

  EXPECT_EQ(snapshot.max_inbound_queue_size, expected_maximum);
  EXPECT_EQ(snapshot.max_outbound_queue_size, expected_maximum);
  EXPECT_EQ(snapshot.max_session_pending_write_bytes, expected_maximum);
}

}  // namespace
