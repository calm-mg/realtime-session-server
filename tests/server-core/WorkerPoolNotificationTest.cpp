#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#include "rss/net/CompletionNotifier.h"
#include "rss/net/OverloadStats.h"
#include "rss/net/WorkerPool.h"
#include "rss/service/MessageRouter.h"
#include "rss/service/RoomService.h"
#include "rss/service/SessionEventHandler.h"
#include "rss/util/BoundedBlockingQueue.h"

namespace {

using namespace std::chrono_literals;

class RecordingNotifier final : public rss::net::CompletionNotifier {
 public:
  void notify() noexcept override { notifications.fetch_add(1); }

  std::atomic<int> notifications{0};
};

class RecordingSessionEventHandler final
    : public rss::service::SessionEventHandler {
 public:
  void handle(const rss::service::SessionEvent& event,
              rss::service::OutboundMessageSink& sink) override {
    handled_session_id.store(event.session_id);
    static_cast<void>(sink.emit({event.session_id, {0x01U}}));
  }

  std::atomic<std::uint64_t> handled_session_id{0};
};

class DelayedLoginHandler final : public rss::service::SessionEventHandler {
 public:
  explicit DelayedLoginHandler(rss::service::RoomService& room_service)
      : router_(room_service) {}

  void handle(const rss::service::SessionEvent& event,
              rss::service::OutboundMessageSink& sink) override {
    if (event.kind == rss::service::SessionEventKind::Packet) {
      std::unique_lock<std::mutex> lock(mutex_);
      login_entered_ = true;
      changed_.notify_all();
      changed_.wait(lock, [this] { return release_login_; });
    } else {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        disconnected_entered_ = true;
      }
      changed_.notify_all();
    }
    router_.handle(event, sink);
  }

  bool waitForLogin() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, 1s, [this] { return login_entered_; });
  }

  bool waitForDisconnected(std::chrono::steady_clock::duration timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout,
                             [this] { return disconnected_entered_; });
  }

  void releaseLogin() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      release_login_ = true;
    }
    changed_.notify_all();
  }

 private:
  rss::service::MessageRouter router_;
  std::mutex mutex_;
  std::condition_variable changed_;
  bool login_entered_{false};
  bool disconnected_entered_{false};
  bool release_login_{false};
};

class BudgetBurstHandler final : public rss::service::SessionEventHandler {
 public:
  explicit BudgetBurstHandler(std::vector<std::vector<std::uint8_t>> outputs)
      : outputs_(std::move(outputs)) {}

  void handle(const rss::service::SessionEvent& event,
              rss::service::OutboundMessageSink& sink) override {
    for (auto& bytes : outputs_) {
      if (!sink.emit({event.session_id, bytes})) {
        break;
      }
      ++accepted_;
    }
  }

  [[nodiscard]] std::size_t accepted() const { return accepted_.load(); }

 private:
  std::vector<std::vector<std::uint8_t>> outputs_;
  std::atomic<std::size_t> accepted_{0};
};

class BlockingNoOutputHandler final : public rss::service::SessionEventHandler {
 public:
  void handle(const rss::service::SessionEvent&,
              rss::service::OutboundMessageSink&) override {
    const auto entered = entered_.fetch_add(1) + 1;
    changed_.notify_all();
    if (entered != 1) {
      return;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait(lock, [this] { return released_; });
  }

  bool waitForFirstEntry() {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, 1s, [this] { return entered_.load() >= 1; });
  }

  void release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      released_ = true;
    }
    changed_.notify_all();
  }

  [[nodiscard]] std::size_t entered() const { return entered_.load(); }

 private:
  std::atomic<std::size_t> entered_{0};
  std::mutex mutex_;
  std::condition_variable changed_;
  bool released_{false};
};

class ThrowingSessionEventHandler final
    : public rss::service::SessionEventHandler {
 public:
  void handle(const rss::service::SessionEvent& event,
              rss::service::OutboundMessageSink& sink) override {
    if (event.session_id == 1) {
      if (event.kind == rss::service::SessionEventKind::Disconnected) {
        disconnected_calls_.fetch_add(1);
        return;
      }
      const auto packet_calls = failed_session_packet_calls_.fetch_add(1) + 1;
      if (packet_calls == 1) {
        static_cast<void>(sink.emit({event.session_id, {0xEEU}}));
        throw std::runtime_error("handler failure");
      }
      return;
    }
    healthy_session_calls_.fetch_add(1);
  }

  [[nodiscard]] std::size_t failedSessionPacketCalls() const {
    return failed_session_packet_calls_.load();
  }

  [[nodiscard]] std::size_t healthySessionCalls() const {
    return healthy_session_calls_.load();
  }

  [[nodiscard]] std::size_t disconnectedCalls() const {
    return disconnected_calls_.load();
  }

 private:
  std::atomic<std::size_t> failed_session_packet_calls_{0};
  std::atomic<std::size_t> healthy_session_calls_{0};
  std::atomic<std::size_t> disconnected_calls_{0};
};

rss::net::WorkerPoolConfig workerConfig(std::size_t inbound_low_watermark = 1) {
  return rss::net::WorkerPoolConfig{
      .inbound_low_watermark = inbound_low_watermark,
      .max_outbound_messages_per_event = 1024,
      .max_outbound_bytes_per_event = 4U * 1024U * 1024U,
  };
}

template <typename Predicate>
bool waitUntil(Predicate&& predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 1s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return predicate();
}

std::optional<rss::service::OutboundMessage> waitForOutbound(
    rss::util::BoundedBlockingQueue<rss::service::OutboundMessage>& outbox) {
  const auto deadline = std::chrono::steady_clock::now() + 1s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto result = outbox.tryPop(); result.value.has_value()) {
      return std::move(result.value);
    }
    std::this_thread::sleep_for(1ms);
  }
  return std::nullopt;
}

TEST(WorkerPoolNotificationTest, ProcessesEventsThroughSessionEventHandler) {
  using rss::service::SessionEvent;
  using rss::service::SessionEventKind;

  rss::util::BoundedBlockingQueue<SessionEvent> inbox(4);
  rss::util::BoundedBlockingQueue<rss::service::OutboundMessage> outbox(4);
  RecordingSessionEventHandler handler;
  RecordingNotifier notifier;
  rss::net::WorkerPool workers(inbox, outbox, handler, workerConfig(),
                               &notifier);

  workers.start(1);
  ASSERT_TRUE(
      inbox.push(SessionEvent{SessionEventKind::Packet, 42, {}}).succeeded);

  const auto message = waitForOutbound(outbox);
  workers.beginStop();
  workers.join();

  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(handler.handled_session_id.load(), 42);
  EXPECT_EQ(message->session_id, 42);
  EXPECT_EQ(message->bytes, (std::vector<std::uint8_t>{0x01U}));
  EXPECT_EQ(notifier.notifications.load(), 1);
}

TEST(WorkerPoolNotificationTest,
     NotifiesInputCapacityOnceWhenPopReachesLowWatermark) {
  using rss::service::SessionEvent;
  using rss::service::SessionEventKind;

  rss::util::BoundedBlockingQueue<SessionEvent> inbox(4);
  rss::util::BoundedBlockingQueue<rss::service::OutboundMessage> outbox(4);
  RecordingSessionEventHandler handler;
  RecordingNotifier outbound_notifier;
  RecordingNotifier input_capacity_notifier;

  for (std::uint64_t session_id = 1; session_id <= 3; ++session_id) {
    ASSERT_TRUE(
        inbox.push(SessionEvent{SessionEventKind::Packet, session_id, {}})
            .succeeded);
  }

  rss::net::WorkerPool workers(inbox, outbox, handler, workerConfig(),
                               &outbound_notifier, &input_capacity_notifier);
  workers.start(1);

  ASSERT_TRUE(
      waitUntil([&] { return outbound_notifier.notifications.load() == 3; }));
  workers.beginStop();
  workers.join();

  EXPECT_EQ(input_capacity_notifier.notifications.load(), 1);
  EXPECT_EQ(outbound_notifier.notifications.load(), 3);
}

TEST(WorkerPoolNotificationTest,
     ForceStopClosesQueuesAndReleasesBlockedOutboundProducer) {
  using rss::service::OutboundMessage;
  using rss::service::SessionEvent;
  using rss::service::SessionEventKind;

  rss::util::BoundedBlockingQueue<SessionEvent> inbox(1);
  rss::util::BoundedBlockingQueue<OutboundMessage> outbox(1);
  RecordingSessionEventHandler handler;
  ASSERT_TRUE(outbox.push(OutboundMessage{99, {0x99U}}).succeeded);

  rss::net::WorkerPool workers(inbox, outbox, handler, workerConfig());
  workers.start(1);
  ASSERT_TRUE(
      inbox.push(SessionEvent{SessionEventKind::Packet, 42, {}}).succeeded);
  ASSERT_TRUE(waitUntil([&] { return outbox.waiterCounts().producers == 1; }));

  workers.forceStop();
  const auto inbox_closed_by_force = inbox.closed();
  const auto outbox_closed_by_force = outbox.closed();
  workers.forceStop();

  const auto finished_before_deadline =
      waitUntil([&] { return workers.finished(); });
  if (!finished_before_deadline) {
    outbox.close();
    inbox.close();
  }
  workers.join();

  EXPECT_TRUE(inbox_closed_by_force);
  EXPECT_TRUE(outbox_closed_by_force);
  EXPECT_TRUE(finished_before_deadline);
}

TEST(WorkerPoolNotificationTest, TracksActiveWorkersUntilTheyExit) {
  rss::util::BoundedBlockingQueue<rss::service::SessionEvent> inbox(2);
  rss::util::BoundedBlockingQueue<rss::service::OutboundMessage> outbox(2);
  RecordingSessionEventHandler handler;
  rss::net::WorkerPool workers(inbox, outbox, handler, workerConfig());

  workers.start(2);
  ASSERT_TRUE(waitUntil([&] { return inbox.waiterCounts().consumers == 2; }));
  EXPECT_FALSE(workers.finished());

  workers.beginStop();
  ASSERT_TRUE(waitUntil([&] { return workers.finished(); }));
  workers.join();
}

TEST(WorkerPoolNotificationTest,
     SerializesPacketAndDisconnectForTheSameSession) {
  using rss::protocol::PacketType;
  using rss::service::SessionEvent;
  using rss::service::SessionEventKind;

  rss::util::BoundedBlockingQueue<SessionEvent> inbox(4);
  rss::util::BoundedBlockingQueue<rss::service::OutboundMessage> outbox(4);
  rss::service::RoomService room_service;
  DelayedLoginHandler handler(room_service);
  rss::net::WorkerPool workers(inbox, outbox, handler, workerConfig());

  rss::protocol::Packet login;
  login.type = PacketType::LoginReq;
  login.payload = {'a', 'l', 'i', 'c', 'e'};

  workers.start(2);
  ASSERT_TRUE(inbox.push(SessionEvent{SessionEventKind::Packet, 42, login, 0})
                  .succeeded);
  ASSERT_TRUE(handler.waitForLogin());
  ASSERT_TRUE(
      inbox.push(SessionEvent{SessionEventKind::Disconnected, 42, {}, 1})
          .succeeded);

  const auto disconnected_overtook_login = handler.waitForDisconnected(100ms);
  handler.releaseLogin();
  workers.beginStop();
  workers.join();

  EXPECT_FALSE(disconnected_overtook_login);
  EXPECT_FALSE(room_service.userOf(42).has_value());
}

TEST(WorkerPoolNotificationTest,
     EnforcesPerEventMessageCountAndObservesOutboundPeak) {
  using rss::service::SessionEvent;
  using rss::service::SessionEventKind;

  rss::util::BoundedBlockingQueue<SessionEvent> inbox(2);
  rss::util::BoundedBlockingQueue<rss::service::OutboundMessage> outbox(4);
  BudgetBurstHandler handler({{1U}, {2U}, {3U}});
  rss::net::OverloadStats stats;
  rss::net::WorkerPool workers(
      inbox, outbox, handler,
      rss::net::WorkerPoolConfig{.inbound_low_watermark = 1,
                                 .max_outbound_messages_per_event = 2,
                                 .max_outbound_bytes_per_event = 16},
      nullptr, nullptr, &stats);

  workers.start(1);
  ASSERT_TRUE(
      inbox.push(SessionEvent{SessionEventKind::Packet, 42, {}}).succeeded);
  ASSERT_TRUE(waitUntil([&] { return handler.accepted() == 2; }));
  workers.beginStop();
  workers.join();

  EXPECT_EQ(outbox.size(), 2U);
  const auto snapshot = stats.snapshot(0, outbox.size(), 0);
  EXPECT_EQ(snapshot.max_outbound_queue_size, 2U);
  EXPECT_EQ(snapshot.outbound_budget_rejections, 1U);
}

TEST(WorkerPoolNotificationTest,
     EnforcesPerEventByteBudgetAndRejectsEmptyOutput) {
  using rss::service::SessionEvent;
  using rss::service::SessionEventKind;

  rss::util::BoundedBlockingQueue<SessionEvent> inbox(4);
  rss::util::BoundedBlockingQueue<rss::service::OutboundMessage> outbox(4);
  BudgetBurstHandler byte_limited({{1U, 2U, 3U}, {4U, 5U}});
  rss::net::WorkerPool byte_workers(
      inbox, outbox, byte_limited,
      rss::net::WorkerPoolConfig{.inbound_low_watermark = 1,
                                 .max_outbound_messages_per_event = 4,
                                 .max_outbound_bytes_per_event = 4});

  byte_workers.start(1);
  ASSERT_TRUE(
      inbox.push(SessionEvent{SessionEventKind::Packet, 1, {}}).succeeded);
  ASSERT_TRUE(waitUntil([&] { return byte_limited.accepted() == 1; }));
  byte_workers.beginStop();
  byte_workers.join();
  EXPECT_EQ(outbox.size(), 1U);

  rss::util::BoundedBlockingQueue<SessionEvent> empty_inbox(2);
  rss::util::BoundedBlockingQueue<rss::service::OutboundMessage> empty_outbox(
      2);
  BudgetBurstHandler empty_output(std::vector<std::vector<std::uint8_t>>{{}});
  rss::net::WorkerPool empty_workers(
      empty_inbox, empty_outbox, empty_output,
      rss::net::WorkerPoolConfig{.inbound_low_watermark = 1,
                                 .max_outbound_messages_per_event = 2,
                                 .max_outbound_bytes_per_event = 4});
  empty_workers.start(1);
  ASSERT_TRUE(empty_inbox.push(SessionEvent{SessionEventKind::Packet, 2, {}})
                  .succeeded);
  ASSERT_TRUE(waitUntil([&] {
    return empty_inbox.size() == 0 && empty_inbox.waiterCounts().consumers == 1;
  }));
  empty_workers.beginStop();
  empty_workers.join();

  EXPECT_EQ(empty_output.accepted(), 0U);
  EXPECT_EQ(empty_outbox.size(), 0U);
}

TEST(WorkerPoolNotificationTest,
     ForceStopSkipsQueuedEventsAfterCurrentHandler) {
  using rss::service::SessionEvent;
  using rss::service::SessionEventKind;

  rss::util::BoundedBlockingQueue<SessionEvent> inbox(4);
  rss::util::BoundedBlockingQueue<rss::service::OutboundMessage> outbox(2);
  BlockingNoOutputHandler handler;
  rss::net::WorkerPool workers(inbox, outbox, handler, workerConfig());

  workers.start(1);
  ASSERT_TRUE(
      inbox.push(SessionEvent{SessionEventKind::Packet, 1, {}, 0}).succeeded);
  ASSERT_TRUE(handler.waitForFirstEntry());
  ASSERT_TRUE(
      inbox.push(SessionEvent{SessionEventKind::Packet, 2, {}, 0}).succeeded);
  ASSERT_TRUE(
      inbox.push(SessionEvent{SessionEventKind::Packet, 3, {}, 0}).succeeded);

  workers.forceStop();
  handler.release();
  ASSERT_TRUE(waitUntil([&] { return workers.finished(); }));
  workers.join();

  EXPECT_EQ(handler.entered(), 1U);
}

TEST(WorkerPoolNotificationTest,
     HandlerExceptionQuarantinesFailedSessionAndKeepsWorkerRunning) {
  using rss::service::OutboundMessageKind;
  using rss::service::SessionEvent;
  using rss::service::SessionEventKind;

  rss::util::BoundedBlockingQueue<SessionEvent> inbox(4);
  rss::util::BoundedBlockingQueue<rss::service::OutboundMessage> outbox(2);
  ThrowingSessionEventHandler handler;
  rss::net::OverloadStats stats;
  rss::net::WorkerPool workers(inbox, outbox, handler, workerConfig(), nullptr,
                               nullptr, &stats);

  workers.start(1);
  ASSERT_TRUE(
      inbox.push(SessionEvent{SessionEventKind::Packet, 1, {}, 0}).succeeded);
  ASSERT_TRUE(
      inbox.push(SessionEvent{SessionEventKind::Packet, 1, {}, 1}).succeeded);
  ASSERT_TRUE(inbox.push(SessionEvent{SessionEventKind::Disconnected, 1, {}, 2})
                  .succeeded);
  ASSERT_TRUE(
      inbox.push(SessionEvent{SessionEventKind::Packet, 2, {}, 0}).succeeded);

  ASSERT_TRUE(waitUntil([&] {
    return handler.disconnectedCalls() == 1 &&
           handler.healthySessionCalls() == 1;
  }));
  const auto disconnect = waitForOutbound(outbox);
  workers.beginStop();
  workers.join();

  EXPECT_EQ(handler.failedSessionPacketCalls(), 1U);
  EXPECT_EQ(handler.healthySessionCalls(), 1U);
  EXPECT_EQ(handler.disconnectedCalls(), 1U);
  ASSERT_TRUE(disconnect.has_value());
  EXPECT_EQ(disconnect->kind, OutboundMessageKind::DisconnectSession);
  EXPECT_EQ(disconnect->session_id, 1U);
  EXPECT_TRUE(disconnect->bytes.empty());
  EXPECT_EQ(stats.snapshot(0, 0, 0).handler_exceptions, 1U);
}

TEST(WorkerPoolNotificationTest, DestructorClosesQueuesAndJoinsWorkers) {
  using rss::service::OutboundMessage;
  using rss::service::SessionEvent;
  using rss::service::SessionEventKind;

  rss::util::BoundedBlockingQueue<SessionEvent> inbox(1);
  rss::util::BoundedBlockingQueue<OutboundMessage> outbox(1);
  RecordingSessionEventHandler handler;
  ASSERT_TRUE(outbox.push(OutboundMessage{99, {0x99U}}).succeeded);

  auto destroyed = std::async(std::launch::async, [&] {
    rss::net::WorkerPool workers(inbox, outbox, handler, workerConfig());
    workers.start(1);
    if (!inbox.push(SessionEvent{SessionEventKind::Packet, 42, {}}).succeeded) {
      return false;
    }
    return waitUntil([&] { return outbox.waiterCounts().producers == 1; });
  });

  const auto destruction_status = destroyed.wait_for(1s);
  if (destruction_status != std::future_status::ready) {
    inbox.close();
    outbox.close();
  }
  ASSERT_EQ(destruction_status, std::future_status::ready);
  EXPECT_TRUE(destroyed.get());
  EXPECT_TRUE(inbox.closed());
  EXPECT_TRUE(outbox.closed());
}

}  // namespace
