#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "EmbeddedServer.h"
#include "ScenarioClient.h"
#include "ScenarioRunner.h"
#include "rss/net/ClientIoError.h"
#include "rss/protocol/ProtocolError.h"

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
  options.payload_bytes = 1291;
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
  options.messages_per_sender = 500;
  options.payload_bytes = 1291;
  options.worker_count = 2;

  rss::tools::ScenarioRunner runner{
      {.slow_client_max_pending_write_bytes = 256U * 1024U,
       .socket_receive_buffer_bytes = 1024,
       .scenario_timeout = std::chrono::seconds{10}}};
  const auto result = runner.runOnce(options, 1);
  EXPECT_GE(result.overload.slow_client_disconnects, 1U);
  EXPECT_EQ(result.sent, 4500U);
  EXPECT_EQ(result.expected_broadcasts, 40500U);
  EXPECT_EQ(result.received_broadcasts, 40500U);
  EXPECT_EQ(result.missing_broadcasts, 0U);
  EXPECT_EQ(result.duplicate_broadcasts, 0U);
  EXPECT_EQ(result.unexpected_broadcasts, 0U);
  EXPECT_EQ(result.failed_clients, 0U);
  EXPECT_EQ(result.latencies.size(), 40500U);
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

TEST(ScenarioRunnerTest, DeadlineReportsMissingBroadcastsAfterSuccessfulSend) {
  using namespace std::chrono_literals;

  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::Broadcast;
  options.clients = 2;
  options.messages_per_sender = 200;
  options.payload_bytes = 128;
  options.worker_count = 2;

  constexpr auto scenario_timeout = 25ms;
  constexpr auto coordination_timeout = 5s;
  std::mutex receiver_mutex;
  std::condition_variable receiver_changed;
  std::size_t waiting_receivers{};
  bool release_receivers{};
  std::atomic_flag deadline_coordinator = ATOMIC_FLAG_INIT;
  std::atomic_bool coordination_failed{};
  const auto releaseAllReceivers = [&] {
    {
      std::lock_guard lock(receiver_mutex);
      release_receivers = true;
    }
    receiver_changed.notify_all();
  };

  const rss::tools::ScenarioRunner runner{{
      .scenario_timeout = scenario_timeout,
      .before_receive =
          [&] {
            std::unique_lock lock(receiver_mutex);
            ++waiting_receivers;
            receiver_changed.notify_all();
            if (!receiver_changed.wait_for(lock, coordination_timeout,
                                           [&] { return release_receivers; })) {
              coordination_failed.store(true);
              lock.unlock();
              releaseAllReceivers();
              throw std::runtime_error("receiver release timed out");
            }
          },
      .before_send =
          [&](std::size_t, std::size_t sequence) {
            if (sequence != 1 || deadline_coordinator.test_and_set()) {
              return;
            }

            {
              std::unique_lock lock(receiver_mutex);
              if (!receiver_changed.wait_for(lock, coordination_timeout, [&] {
                    return waiting_receivers == options.clients;
                  })) {
                coordination_failed.store(true);
                lock.unlock();
                releaseAllReceivers();
                throw std::runtime_error("receiver startup timed out");
              }
            }
            std::this_thread::sleep_for(scenario_timeout);
            releaseAllReceivers();
          },
  }};
  const auto result = runner.runOnce(options, 1);

  EXPECT_GT(result.sent, 0U);
  EXPECT_GT(result.expected_broadcasts, 0U);
  EXPECT_EQ(result.received_broadcasts, 0U);
  EXPECT_EQ(result.missing_broadcasts, result.expected_broadcasts);
  EXPECT_FALSE(coordination_failed.load());
  EXPECT_NE(rss::tools::formatRunResult(1, options.scenario, result)
                .find("client_receive_timeout=2 "),
            std::string::npos);
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
  EXPECT_NE(rss::tools::formatRunResult(1, options.scenario, result)
                .find("client_send_other=1 "),
            std::string::npos);
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
  EXPECT_EQ(result.client_failures.setup.peer_closed +
                result.client_failures.setup.socket_error,
            1U);
  EXPECT_EQ(result.client_failures.setup.other, 0U);
  EXPECT_EQ(result.client_failures.send.other, 0U);
  EXPECT_EQ(result.client_failures.receive.other, 0U);
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

TEST(ScenarioRunnerTest, RejectsUnboundedReceiptAllocationBeforeStarting) {
  rss::tools::ScenarioOptions options;
  options.clients = 2;
  options.messages_per_sender = std::numeric_limits<std::size_t>::max();
  EXPECT_THROW(
      static_cast<void>(rss::tools::ScenarioRunner{}.runOnce(options, 1)),
      std::invalid_argument);
}

TEST(ScenarioRunnerTest, CountsBothFailingStagesWithoutCountingClientTwice) {
  rss::tools::ScenarioOptions options;
  options.clients = 1;
  options.messages_per_sender = 1;
  options.worker_count = 1;
  rss::tools::ScenarioTuning tuning;
  tuning.before_measurement_start = [] { throw 7; };
  const auto result = rss::tools::ScenarioRunner{tuning}.runOnce(options, 1);
  EXPECT_EQ(result.failed_clients, 1U);
  const auto report = rss::tools::formatRunResult(1, options.scenario, result);
  EXPECT_NE(report.find("client_send_other=1 "), std::string::npos);
  EXPECT_NE(report.find("client_receive_other=1 "), std::string::npos);
}

TEST(ScenarioRunnerTest, ClassifiesReceiveHookAndSendHookIndependently) {
  rss::tools::ScenarioOptions options;
  options.clients = 1;
  options.messages_per_sender = 1;
  options.worker_count = 1;
  rss::tools::ScenarioTuning tuning;
  tuning.before_receive = [] {
    throw rss::protocol::ProtocolError("private protocol detail");
  };
  tuning.before_send = [](std::size_t, std::size_t) {
    throw rss::net::ClientIoError(rss::net::ClientIoFailure::SocketError,
                                  "private socket detail");
  };
  const auto result = rss::tools::ScenarioRunner{tuning}.runOnce(options, 1);
  EXPECT_EQ(result.failed_clients, 1U);
  EXPECT_EQ(result.client_failures.send.socket_error, 1U);
  EXPECT_EQ(result.client_failures.receive.protocol, 1U);
  EXPECT_EQ(result.client_failures.send.other, 0U);
  EXPECT_EQ(result.client_failures.receive.other, 0U);
  EXPECT_EQ(
      rss::tools::formatRunResult(1, options.scenario, result).find("private"),
      std::string::npos);
}

TEST(ScenarioRunnerTest, SetupValidationFailureCountsOnlyObservedFailure) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::SlowClient;
  options.clients = 3;
  options.slow_clients = 1;
  options.messages_per_sender = 1;
  options.worker_count = 1;
  rss::tools::ScenarioTuning tuning;
  tuning.socket_receive_buffer_bytes = 0;
  const auto result = rss::tools::ScenarioRunner{tuning}.runOnce(options, 1);
  EXPECT_EQ(result.failed_clients, 1U);
  EXPECT_EQ(result.client_failures.setup.other, 1U);
  EXPECT_EQ(result.client_failures.send.other, 0U);
  EXPECT_EQ(result.client_failures.receive.other, 0U);
  EXPECT_EQ(result.sent, 0U);
}

TEST(ScenarioRunnerTest, PacingSpacesSendsAfterDelayedSendWithoutCatchUp) {
  using namespace std::chrono_literals;
  rss::tools::ScenarioOptions options;
  options.clients = 1;
  options.messages_per_sender = 3;
  options.rate_per_client = 20;
  std::vector<std::chrono::steady_clock::time_point> sends;
  rss::tools::ScenarioTuning tuning;
  tuning.before_send = [&](std::size_t, std::size_t sequence) {
    sends.push_back(std::chrono::steady_clock::now());
    if (sequence == 0) {
      std::this_thread::sleep_for(100ms);
    }
  };
  const auto result = rss::tools::ScenarioRunner{tuning}.runOnce(options, 1);
  ASSERT_EQ(sends.size(), 3U);
  EXPECT_GE(sends[1] - sends[0], 150ms);
  EXPECT_GE(sends[2] - sends[1], 50ms);
  EXPECT_EQ(result.sent, 3U);
  EXPECT_EQ(result.received_broadcasts, 3U);
  EXPECT_EQ(result.failed_clients, 0U);
}

TEST(ScenarioRunnerTest, WindowBoundsRoomRequestsUntilFastReadersAdvance) {
  using namespace std::chrono_literals;
  rss::tools::ScenarioOptions options;
  options.clients = 3;
  options.messages_per_sender = 3;
  options.max_in_flight = 2;
  std::atomic_size_t attempts{};
  std::atomic_size_t observed_max{};
  rss::tools::ScenarioTuning tuning;
  tuning.before_receive = [&] {
    std::this_thread::sleep_for(100ms);
    const auto observed = attempts.load();
    auto previous = observed_max.load();
    while (previous < observed &&
           !observed_max.compare_exchange_weak(previous, observed)) {
    }
  };
  tuning.before_send = [&](std::size_t, std::size_t) { ++attempts; };
  const auto result = rss::tools::ScenarioRunner{tuning}.runOnce(options, 1);
  EXPECT_LE(observed_max.load(), 2U);
  EXPECT_EQ(result.sent, 9U);
  EXPECT_EQ(result.received_broadcasts, 27U);
  EXPECT_EQ(result.failed_clients, 0U);
  EXPECT_EQ(result.effective_max_in_flight, 2U);
}

TEST(ScenarioRunnerTest, WindowUsesIndependentTicketsForUnequalRooms) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::MultiRoom;
  options.clients = 5;
  options.rooms = 2;
  options.messages_per_sender = 3;
  options.max_in_flight = 1;
  const auto result = rss::tools::ScenarioRunner{}.runOnce(options, 1);
  EXPECT_EQ(result.sent, 15U);
  EXPECT_EQ(result.received_broadcasts, 39U);
  EXPECT_EQ(result.failed_clients, 0U);
  EXPECT_EQ(result.effective_max_in_flight, 1U);
}

TEST(ScenarioRunnerTest, PacingWaitStopsAtMeasurementDeadline) {
  using namespace std::chrono_literals;
  rss::tools::ScenarioOptions options;
  options.clients = 1;
  options.messages_per_sender = 2;
  options.rate_per_client = 1;
  const rss::tools::ScenarioRunner runner{{.scenario_timeout = 50ms}};
  const auto result = runner.runOnce(options, 1);
  EXPECT_EQ(result.sent, 1U);
  EXPECT_EQ(result.expected_broadcasts, 1U);
  EXPECT_EQ(result.client_failures.send.timeout, 1U);
  EXPECT_LT(result.elapsed, 500ms);
}

TEST(ScenarioRunnerTest, PacingWaitCancelsWhenFastReceiverFails) {
  using namespace std::chrono_literals;
  rss::tools::ScenarioOptions options;
  options.clients = 1;
  options.messages_per_sender = 2;
  options.rate_per_client = 1;
  rss::tools::ScenarioTuning tuning;
  tuning.before_receive = [] {
    std::this_thread::sleep_for(50ms);
    throw std::runtime_error("injected receiver failure");
  };
  const auto result = rss::tools::ScenarioRunner{tuning}.runOnce(options, 1);
  EXPECT_LE(result.sent, 1U);
  EXPECT_EQ(result.failed_clients, 1U);
  EXPECT_LT(result.elapsed, 500ms);
}

TEST(ScenarioRunnerTest, RejectsSlowWindowAboveSafePendingCapacity) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::SlowClient;
  options.clients = 2;
  options.slow_clients = 1;
  options.max_in_flight = 100;
  EXPECT_THROW(
      static_cast<void>(rss::tools::ScenarioRunner{}.runOnce(options, 1)),
      std::invalid_argument);
}

TEST(ScenarioRunnerTest, UsesRequestedTimeoutUnlessTuningOverridesIt) {
  rss::tools::ScenarioOptions options;
  options.clients = 1;
  options.messages_per_sender = 1;
  options.timeout_seconds = 2;
  const auto result = rss::tools::ScenarioRunner{}.runOnce(options, 1);
  EXPECT_EQ(result.effective_timeout_ms, 2000U);
  const rss::tools::ScenarioRunner runner{
      {.scenario_timeout = std::chrono::milliseconds{500}}};
  EXPECT_EQ(runner.runOnce(options, 2).effective_timeout_ms, 500U);
}

TEST(ScenarioRunnerTest, FailedRoomDoesNotCancelOtherRoomsWindow) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::MultiRoom;
  options.clients = 4;
  options.rooms = 2;
  options.messages_per_sender = 3;
  options.max_in_flight = 1;
  std::atomic_size_t other_room_attempts{};
  rss::tools::ScenarioTuning tuning;
  tuning.scenario_timeout = std::chrono::milliseconds{500};
  tuning.before_send = [&](std::size_t sender, std::size_t) {
    if (sender == 0) {
      throw std::runtime_error("injected room failure");
    }
    if (sender % 2 == 1) {
      ++other_room_attempts;
    }
  };
  const auto result = rss::tools::ScenarioRunner{tuning}.runOnce(options, 1);
  EXPECT_EQ(other_room_attempts.load(), 6U);
  EXPECT_EQ(result.sent, 6U);
  EXPECT_EQ(result.received_broadcasts, 12U);
  EXPECT_EQ(result.failed_clients, 2U);
}

TEST(ScenarioRunnerTest,
     ExternalTargetUsesRequestedServerAndReportsNoSnapshot) {
  rss::net::ServerConfig config;
  config.host = "127.0.0.1";
  config.port = 0;
  config.max_sessions = 1;
  rss::tools::EmbeddedServer server(config);
  server.start(std::chrono::seconds{2});
  rss::tools::ScenarioOptions options;
  options.host = "127.0.0.1";
  options.port = server.port();
  options.clients = 2;
  options.messages_per_sender = 1;
  const auto result = rss::tools::ScenarioRunner{}.runOnce(options, 1);
  EXPECT_FALSE(result.server_stats_available);
  EXPECT_EQ(result.failed_clients, 2U);
  EXPECT_EQ(result.sent, 0U);
  // close가 먼저 관측돼도 I/O 스레드의 거절 통계 갱신까지 기다린다.
  server.stop();
  EXPECT_GE(server.snapshot().rejected_connections, 1U);
}

TEST(ScenarioRunnerTest,
     ExternalTargetSupportsRepeatedUnevenRoomsAndStaysAlive) {
  rss::net::ServerConfig config;
  config.host = "127.0.0.1";
  config.port = 0;
  rss::tools::EmbeddedServer server(config);
  server.start(std::chrono::seconds{2});
  rss::tools::ScenarioOptions options;
  options.host = "127.0.0.1";
  options.port = server.port();
  options.scenario = rss::tools::ScenarioKind::MultiRoom;
  options.clients = 3;
  options.rooms = 2;
  options.messages_per_sender = 3;
  options.max_in_flight = 2;
  for (std::size_t run = 0; run < 3; ++run) {
    const auto result = rss::tools::ScenarioRunner{}.runOnce(options, run);
    EXPECT_FALSE(result.server_stats_available);
    EXPECT_TRUE(rss::tools::isSuccessful(options.scenario, result, 0));
    EXPECT_EQ(result.sent, 9U);
    EXPECT_EQ(result.received_broadcasts, 15U);
  }
  rss::tools::ScenarioClient probe;
  EXPECT_NO_THROW(
      probe.connect("127.0.0.1", server.port(), std::chrono::seconds{2}));
}
