#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <limits>

#include "ScenarioRunner.h"

TEST(ScenarioRunnerTest, BroadcastDeliversEveryMessageToEveryClient) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::Broadcast;
  options.clients = 2;
  options.messages_per_sender = 3;
  options.payload_bytes = 128;
  options.worker_count = 2;

  const auto result = rss::tools::ScenarioRunner{}.runOnce(options, 1);
  EXPECT_EQ(result.sent, 6U);
  EXPECT_EQ(result.expected_broadcasts, 12U);
  EXPECT_EQ(result.received_broadcasts, 12U);
  EXPECT_EQ(result.missing_broadcasts, 0U);
  EXPECT_EQ(result.duplicate_broadcasts, 0U);
  EXPECT_EQ(result.unexpected_broadcasts, 0U);
  EXPECT_EQ(result.failed_clients, 0U);
  EXPECT_EQ(result.latencies.size(), 12U);
}

TEST(ScenarioRunnerTest, MultiRoomKeepsBroadcastsInsideEachRoom) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::MultiRoom;
  options.clients = 4;
  options.rooms = 2;
  options.messages_per_sender = 2;
  options.payload_bytes = 128;

  const auto result = rss::tools::ScenarioRunner{}.runOnce(options, 1);
  EXPECT_EQ(result.sent, 8U);
  EXPECT_EQ(result.expected_broadcasts, 16U);
  EXPECT_EQ(result.received_broadcasts, 16U);
  EXPECT_EQ(result.missing_broadcasts, 0U);
  EXPECT_EQ(result.unexpected_broadcasts, 0U);
}

TEST(ScenarioRunnerTest, MessageIdentityRoundTripsAtRequestedPayloadSize) {
  const auto payload = rss::tools::makeScenarioPayload(2, 3, 4, 123456, 128);
  EXPECT_EQ(payload.size(), 128U);
  const auto identity = rss::tools::parseScenarioPayload(payload);
  EXPECT_EQ(identity.run, 2U);
  EXPECT_EQ(identity.sender, 3U);
  EXPECT_EQ(identity.sequence, 4U);
  EXPECT_EQ(identity.sent_us, 123456U);
}

TEST(ScenarioRunnerTest, DeadlineReturnsMissingBroadcasts) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::Broadcast;
  options.clients = 2;
  options.messages_per_sender = 200;
  options.payload_bytes = 128;
  options.worker_count = 2;

  const rss::tools::ScenarioRunner runner{std::chrono::milliseconds{1}};
  const auto result = runner.runOnce(options, 1);
  EXPECT_GT(result.missing_broadcasts, 0U);
}

TEST(ScenarioRunnerTestDeathTest, ReceiverCountOverflowIsCaptured) {
  EXPECT_EXIT(
      {
        rss::tools::ScenarioOptions options;
        options.scenario = rss::tools::ScenarioKind::Broadcast;
        options.clients = 2;
        options.messages_per_sender = std::numeric_limits<std::size_t>::max();
        options.payload_bytes = 128;
        options.worker_count = 2;

        const rss::tools::ScenarioRunner runner{std::chrono::milliseconds{1}};
        const auto result = runner.runOnce(options, 1);
        std::_Exit(result.failed_clients == 2U ? EXIT_SUCCESS : EXIT_FAILURE);
      },
      ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}
