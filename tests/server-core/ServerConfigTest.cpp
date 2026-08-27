#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <stdexcept>

#include "rss/net/ServerConfig.h"

namespace {

using rss::net::ServerConfig;

TEST(ServerConfigTest, UsesBackpressureDefaults) {
  const ServerConfig config;

  EXPECT_EQ(config.inbound_queue_capacity, 4096U);
  EXPECT_EQ(config.inbound_high_watermark, 3072U);
  EXPECT_EQ(config.inbound_low_watermark, 2048U);
  EXPECT_EQ(config.outbound_queue_capacity, 4096U);
  EXPECT_EQ(config.max_outbound_messages_per_event, 10000U);
  EXPECT_EQ(config.max_outbound_bytes_per_event, 40U * 1024U * 1024U);
  EXPECT_EQ(config.max_parked_events_per_session, 32U);
  EXPECT_EQ(config.max_pending_write_bytes, 1024U * 1024U);
  EXPECT_EQ(config.max_sessions, 10000U);
  EXPECT_EQ(config.graceful_shutdown_timeout, std::chrono::seconds{5});
  EXPECT_NO_THROW(config.validate());
}

TEST(ServerConfigTest, AcceptsHighWatermarkEqualToInboundCapacity) {
  ServerConfig config;
  config.inbound_queue_capacity = 3;
  config.inbound_high_watermark = 3;
  config.inbound_low_watermark = 1;

  EXPECT_NO_THROW(config.validate());
}

TEST(ServerConfigTest, AcceptsMinimumOperationalValuesAndEphemeralPort) {
  ServerConfig config;
  config.host = "127.0.0.1";
  config.port = 0;
  config.worker_count = 1;
  config.backlog = 1;
  config.max_events = 1;
  config.idle_timeout = std::chrono::seconds{1};

  EXPECT_NO_THROW(config.validate());
}

enum class InvalidConfigCase {
  EmptyHost,
  ZeroWorkerCount,
  ZeroBacklog,
  NegativeBacklog,
  ZeroMaxEvents,
  NegativeMaxEvents,
  ZeroIdleTimeout,
  NegativeIdleTimeout,
  ZeroInboundCapacity,
  ZeroLowWatermark,
  EqualWatermarks,
  ReversedWatermarks,
  HighWatermarkAboveCapacity,
  ZeroOutboundCapacity,
  ZeroOutboundMessagesPerEvent,
  ZeroOutboundBytesPerEvent,
  ZeroParkedEventsPerSession,
  ZeroPendingWriteBytes,
  ZeroMaxSessions,
  ZeroShutdownTimeout,
  NegativeShutdownTimeout,
};

const char* invalidConfigCaseName(
    const testing::TestParamInfo<InvalidConfigCase>& info) {
  switch (info.param) {
    case InvalidConfigCase::EmptyHost:
      return "EmptyHost";
    case InvalidConfigCase::ZeroWorkerCount:
      return "ZeroWorkerCount";
    case InvalidConfigCase::ZeroBacklog:
      return "ZeroBacklog";
    case InvalidConfigCase::NegativeBacklog:
      return "NegativeBacklog";
    case InvalidConfigCase::ZeroMaxEvents:
      return "ZeroMaxEvents";
    case InvalidConfigCase::NegativeMaxEvents:
      return "NegativeMaxEvents";
    case InvalidConfigCase::ZeroIdleTimeout:
      return "ZeroIdleTimeout";
    case InvalidConfigCase::NegativeIdleTimeout:
      return "NegativeIdleTimeout";
    case InvalidConfigCase::ZeroInboundCapacity:
      return "ZeroInboundCapacity";
    case InvalidConfigCase::ZeroLowWatermark:
      return "ZeroLowWatermark";
    case InvalidConfigCase::EqualWatermarks:
      return "EqualWatermarks";
    case InvalidConfigCase::ReversedWatermarks:
      return "ReversedWatermarks";
    case InvalidConfigCase::HighWatermarkAboveCapacity:
      return "HighWatermarkAboveCapacity";
    case InvalidConfigCase::ZeroOutboundCapacity:
      return "ZeroOutboundCapacity";
    case InvalidConfigCase::ZeroOutboundMessagesPerEvent:
      return "ZeroOutboundMessagesPerEvent";
    case InvalidConfigCase::ZeroOutboundBytesPerEvent:
      return "ZeroOutboundBytesPerEvent";
    case InvalidConfigCase::ZeroParkedEventsPerSession:
      return "ZeroParkedEventsPerSession";
    case InvalidConfigCase::ZeroPendingWriteBytes:
      return "ZeroPendingWriteBytes";
    case InvalidConfigCase::ZeroMaxSessions:
      return "ZeroMaxSessions";
    case InvalidConfigCase::ZeroShutdownTimeout:
      return "ZeroShutdownTimeout";
    case InvalidConfigCase::NegativeShutdownTimeout:
      return "NegativeShutdownTimeout";
  }
  return "Unknown";
}

class InvalidServerConfigTest
    : public testing::TestWithParam<InvalidConfigCase> {};

TEST_P(InvalidServerConfigTest, RejectsInvalidValue) {
  ServerConfig config;

  switch (GetParam()) {
    case InvalidConfigCase::EmptyHost:
      config.host.clear();
      break;
    case InvalidConfigCase::ZeroWorkerCount:
      config.worker_count = 0;
      break;
    case InvalidConfigCase::ZeroBacklog:
      config.backlog = 0;
      break;
    case InvalidConfigCase::NegativeBacklog:
      config.backlog = -1;
      break;
    case InvalidConfigCase::ZeroMaxEvents:
      config.max_events = 0;
      break;
    case InvalidConfigCase::NegativeMaxEvents:
      config.max_events = -1;
      break;
    case InvalidConfigCase::ZeroIdleTimeout:
      config.idle_timeout = std::chrono::seconds{0};
      break;
    case InvalidConfigCase::NegativeIdleTimeout:
      config.idle_timeout = std::chrono::seconds{-1};
      break;
    case InvalidConfigCase::ZeroInboundCapacity:
      config.inbound_queue_capacity = 0;
      break;
    case InvalidConfigCase::ZeroLowWatermark:
      config.inbound_low_watermark = 0;
      break;
    case InvalidConfigCase::EqualWatermarks:
      config.inbound_low_watermark = config.inbound_high_watermark;
      break;
    case InvalidConfigCase::ReversedWatermarks:
      config.inbound_low_watermark = config.inbound_high_watermark + 1;
      break;
    case InvalidConfigCase::HighWatermarkAboveCapacity:
      config.inbound_high_watermark = config.inbound_queue_capacity + 1;
      break;
    case InvalidConfigCase::ZeroOutboundCapacity:
      config.outbound_queue_capacity = 0;
      break;
    case InvalidConfigCase::ZeroOutboundMessagesPerEvent:
      config.max_outbound_messages_per_event = 0;
      break;
    case InvalidConfigCase::ZeroOutboundBytesPerEvent:
      config.max_outbound_bytes_per_event = 0;
      break;
    case InvalidConfigCase::ZeroParkedEventsPerSession:
      config.max_parked_events_per_session = 0;
      break;
    case InvalidConfigCase::ZeroPendingWriteBytes:
      config.max_pending_write_bytes = 0;
      break;
    case InvalidConfigCase::ZeroMaxSessions:
      config.max_sessions = 0;
      break;
    case InvalidConfigCase::ZeroShutdownTimeout:
      config.graceful_shutdown_timeout = std::chrono::seconds{0};
      break;
    case InvalidConfigCase::NegativeShutdownTimeout:
      config.graceful_shutdown_timeout = std::chrono::seconds{-1};
      break;
  }

  EXPECT_THROW(config.validate(), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(
    InvalidValues, InvalidServerConfigTest,
    testing::Values(
        InvalidConfigCase::EmptyHost, InvalidConfigCase::ZeroWorkerCount,
        InvalidConfigCase::ZeroBacklog, InvalidConfigCase::NegativeBacklog,
        InvalidConfigCase::ZeroMaxEvents, InvalidConfigCase::NegativeMaxEvents,
        InvalidConfigCase::ZeroIdleTimeout,
        InvalidConfigCase::NegativeIdleTimeout,
        InvalidConfigCase::ZeroInboundCapacity,
        InvalidConfigCase::ZeroLowWatermark, InvalidConfigCase::EqualWatermarks,
        InvalidConfigCase::ReversedWatermarks,
        InvalidConfigCase::HighWatermarkAboveCapacity,
        InvalidConfigCase::ZeroOutboundCapacity,
        InvalidConfigCase::ZeroOutboundMessagesPerEvent,
        InvalidConfigCase::ZeroOutboundBytesPerEvent,
        InvalidConfigCase::ZeroParkedEventsPerSession,
        InvalidConfigCase::ZeroPendingWriteBytes,
        InvalidConfigCase::ZeroMaxSessions,
        InvalidConfigCase::ZeroShutdownTimeout,
        InvalidConfigCase::NegativeShutdownTimeout),
    invalidConfigCaseName);

}  // namespace
