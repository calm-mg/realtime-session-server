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
  EXPECT_EQ(config.max_pending_write_bytes, 1024U * 1024U);
  EXPECT_EQ(config.max_sessions, 10000U);
  EXPECT_EQ(config.graceful_shutdown_timeout, std::chrono::seconds{5});
  EXPECT_NO_THROW(config.validate());
}

enum class InvalidConfigCase {
  ZeroInboundCapacity,
  ZeroLowWatermark,
  EqualWatermarks,
  ReversedWatermarks,
  HighWatermarkAboveCapacity,
  ZeroOutboundCapacity,
  ZeroPendingWriteBytes,
  ZeroMaxSessions,
  ZeroShutdownTimeout,
  NegativeShutdownTimeout,
};

const char* invalidConfigCaseName(
    const testing::TestParamInfo<InvalidConfigCase>& info) {
  switch (info.param) {
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

TEST_P(InvalidServerConfigTest, RejectsInvalidBackpressureValue) {
  ServerConfig config;

  switch (GetParam()) {
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
    InvalidBackpressureValues, InvalidServerConfigTest,
    testing::Values(InvalidConfigCase::ZeroInboundCapacity,
                    InvalidConfigCase::ZeroLowWatermark,
                    InvalidConfigCase::EqualWatermarks,
                    InvalidConfigCase::ReversedWatermarks,
                    InvalidConfigCase::HighWatermarkAboveCapacity,
                    InvalidConfigCase::ZeroOutboundCapacity,
                    InvalidConfigCase::ZeroPendingWriteBytes,
                    InvalidConfigCase::ZeroMaxSessions,
                    InvalidConfigCase::ZeroShutdownTimeout,
                    InvalidConfigCase::NegativeShutdownTimeout),
    invalidConfigCaseName);

}  // namespace
