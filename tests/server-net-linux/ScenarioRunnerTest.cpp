#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <thread>

#include "ScenarioRunner.h"

TEST(ScenarioRunnerTest, BroadcastDeliversEveryMessageToEveryClient) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::Broadcast;
  options.clients = 2;
  options.messages_per_sender = 3;
  options.payload_bytes = 128;
  options.worker_count = 2;

  const auto result = rss::tools::ScenarioRunner{}.runOnce(options, 1);
  EXPECT_EQ(result.requested.clients, 2U);
  EXPECT_EQ(result.requested.messages_per_sender, 3U);
  EXPECT_EQ(result.requested.payload_bytes, 128U);
  EXPECT_EQ(result.requested.slow_clients, 1U);
  EXPECT_EQ(result.requested.repeats, 5U);
  EXPECT_EQ(result.effective_rooms, 1U);
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
  EXPECT_EQ(result.effective_rooms, 2U);
  EXPECT_EQ(result.sent, 8U);
  EXPECT_EQ(result.expected_broadcasts, 16U);
  EXPECT_EQ(result.received_broadcasts, 16U);
  EXPECT_EQ(result.missing_broadcasts, 0U);
  EXPECT_EQ(result.unexpected_broadcasts, 0U);
}

TEST(ScenarioRunnerTest,
     DefaultSlowClientLimitDisconnectsSlowClientWithoutFastErrors) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::SlowClient;
  options.clients = 3;
  options.slow_clients = 1;
  options.messages_per_sender = 2000;
  options.payload_bytes = 4000;
  options.worker_count = 2;

  const auto result = rss::tools::ScenarioRunner{}.runOnce(options, 1);
  EXPECT_GE(result.overload.slow_client_disconnects, 1U);
  EXPECT_LE(result.overload.max_session_pending_write_bytes, 32U * 1024U);
  EXPECT_EQ(result.sent, 4000U);
  EXPECT_EQ(result.expected_broadcasts, 8000U);
  EXPECT_EQ(result.received_broadcasts, 8000U);
  EXPECT_EQ(result.missing_broadcasts, 0U);
  EXPECT_EQ(result.duplicate_broadcasts, 0U);
  EXPECT_EQ(result.unexpected_broadcasts, 0U);
  EXPECT_EQ(result.failed_clients, 0U);
  EXPECT_EQ(result.latencies.size(), 8000U);
}

TEST(ScenarioRunnerTest, SharedWindowKeepsFastClientsWithinPendingCapacity) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::SlowClient;
  options.clients = 10;
  options.slow_clients = 1;
  options.messages_per_sender = 100;
  options.payload_bytes = 4000;
  options.worker_count = 2;

  rss::tools::ScenarioRunner runner{
      {.max_pending_write_bytes = 32U * 1024U,
       .socket_receive_buffer_bytes = 1024,
       .scenario_timeout = std::chrono::seconds{10}}};
  const auto result = runner.runOnce(options, 1);
  EXPECT_GE(result.overload.slow_client_disconnects, 1U);
  EXPECT_EQ(result.sent, 900U);
  EXPECT_EQ(result.expected_broadcasts, 8100U);
  EXPECT_EQ(result.received_broadcasts, 8100U);
  EXPECT_EQ(result.missing_broadcasts, 0U);
  EXPECT_EQ(result.duplicate_broadcasts, 0U);
  EXPECT_EQ(result.unexpected_broadcasts, 0U);
  EXPECT_EQ(result.failed_clients, 0U);
  EXPECT_EQ(result.latencies.size(), 8100U);
}

TEST(ScenarioRunnerTest, RejectsZeroSlowClients) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::SlowClient;
  options.clients = 2;
  options.slow_clients = 0;
  options.messages_per_sender = 1;
  options.payload_bytes = 128;

  const rss::tools::ScenarioRunner runner{
      {.scenario_timeout = std::chrono::milliseconds{1}}};
  EXPECT_THROW(static_cast<void>(runner.runOnce(options, 1)),
               std::invalid_argument);
}

TEST(ScenarioRunnerTest, RejectsSlowClientCountEqualToClientCount) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::SlowClient;
  options.clients = 2;
  options.slow_clients = 2;

  EXPECT_THROW(
      static_cast<void>(rss::tools::ScenarioRunner{}.runOnce(options, 1)),
      std::invalid_argument);
}

TEST(ScenarioRunnerTest, RejectsSlowClientCountAboveClientCount) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::SlowClient;
  options.clients = 2;
  options.slow_clients = 3;

  EXPECT_THROW(
      static_cast<void>(rss::tools::ScenarioRunner{}.runOnce(options, 1)),
      std::invalid_argument);
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

  const rss::tools::ScenarioRunner runner{
      {.scenario_timeout = std::chrono::milliseconds{1}}};
  const auto result = runner.runOnce(options, 1);
  EXPECT_GT(result.missing_broadcasts, 0U);
}

TEST(ScenarioRunnerTest, StartsElapsedTimeWhenFinalBarrierParticipantArrives) {
  using namespace std::chrono_literals;

  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::Broadcast;
  options.clients = 1;
  options.messages_per_sender = 1;
  options.payload_bytes = 128;
  options.worker_count = 1;

  std::atomic_flag delayed = ATOMIC_FLAG_INIT;
  constexpr auto setup_delay = 250ms;
  const rss::tools::ScenarioTuning tuning{
      .scenario_timeout = 100ms,
      .before_measurement_start =
          [&] {
            if (!delayed.test_and_set()) {
              std::this_thread::sleep_for(setup_delay);
            }
          },
  };
  const rss::tools::ScenarioRunner runner{tuning};

  const auto result = runner.runOnce(options, 1);

  EXPECT_EQ(result.sent, 1U);
  EXPECT_EQ(result.expected_broadcasts, 1U);
  EXPECT_EQ(result.received_broadcasts, 1U);
  EXPECT_EQ(result.failed_clients, 0U);
  EXPECT_LT(result.elapsed,
            std::chrono::duration_cast<std::chrono::microseconds>(setup_delay));
}

TEST(ScenarioRunnerTest, ExpectedBroadcastsTrackOnlySuccessfulSends) {
  using namespace std::chrono_literals;

  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::Broadcast;
  options.clients = 2;
  options.messages_per_sender = 3;
  options.payload_bytes = 128;
  options.worker_count = 2;

  const rss::tools::ScenarioTuning tuning{
      .scenario_timeout = 200ms,
      .before_send =
          [](std::size_t sender, std::size_t sequence) {
            if (sender == 0 && sequence == 1) {
              throw std::runtime_error("injected partial send failure");
            }
          },
  };
  const auto result = rss::tools::ScenarioRunner{tuning}.runOnce(options, 1);

  EXPECT_EQ(result.sent, 4U);
  EXPECT_EQ(result.expected_broadcasts, 8U);
  EXPECT_EQ(result.received_broadcasts, 8U);
  EXPECT_EQ(result.missing_broadcasts, 0U);
  EXPECT_EQ(result.duplicate_broadcasts, 0U);
  EXPECT_EQ(result.unexpected_broadcasts, 0U);
  EXPECT_EQ(result.failed_clients, 1U);
}

TEST(ScenarioRunnerTest, ClientSetupFailureReturnsMeasurementFailureResult) {
  using namespace std::chrono_literals;

  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::Broadcast;
  options.clients = 2;
  options.messages_per_sender = 1;
  options.payload_bytes = 128;
  options.worker_count = 1;

  const rss::tools::ScenarioTuning tuning{
      .max_sessions = 1,
      .scenario_timeout = 200ms,
  };

  const auto result = rss::tools::ScenarioRunner{tuning}.runOnce(options, 1);

  EXPECT_EQ(result.requested.clients, 2U);
  EXPECT_EQ(result.effective_rooms, 1U);
  EXPECT_EQ(result.sent, 0U);
  EXPECT_EQ(result.expected_broadcasts, 0U);
  EXPECT_EQ(result.received_broadcasts, 0U);
  EXPECT_EQ(result.failed_clients, 2U);
  EXPECT_GE(result.overload.rejected_connections, 1U);
}

TEST(ScenarioRunnerTestDeathTest,
     StartHookFailureIsCapturedWithoutBreakingBarrierLifetime) {
  EXPECT_EXIT(
      {
        rss::tools::ScenarioOptions options;
        options.scenario = rss::tools::ScenarioKind::Broadcast;
        options.clients = 1;
        options.messages_per_sender = 1;
        options.payload_bytes = 128;
        options.worker_count = 1;

        rss::tools::ScenarioTuning tuning;
        tuning.scenario_timeout = std::chrono::milliseconds{100};
        tuning.before_measurement_start = [] {
          throw std::runtime_error("injected start hook failure");
        };
        const auto result =
            rss::tools::ScenarioRunner{tuning}.runOnce(options, 1);
        std::_Exit(result.failed_clients == 1U ? EXIT_SUCCESS : EXIT_FAILURE);
      },
      ::testing::ExitedWithCode(EXIT_SUCCESS), "");
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

        const rss::tools::ScenarioRunner runner{
            {.scenario_timeout = std::chrono::milliseconds{1}}};
        const auto result = runner.runOnce(options, 1);
        std::_Exit(result.failed_clients == 2U ? EXIT_SUCCESS : EXIT_FAILURE);
      },
      ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}
