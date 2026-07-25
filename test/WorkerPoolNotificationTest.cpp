#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <optional>
#include <thread>
#include <utility>

#include "rss/net/CompletionNotifier.h"
#include "rss/net/WorkerPool.h"
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
  std::vector<rss::service::OutboundMessage> handle(
      const rss::service::SessionEvent& event) override {
    handled_session_id.store(event.session_id);
    return {{event.session_id, {0x01U}}};
  }

  std::atomic<std::uint64_t> handled_session_id{0};
};

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
  rss::net::WorkerPool workers(inbox, outbox, handler, 1, &notifier);

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

  rss::net::WorkerPool workers(inbox, outbox, handler, 1, &outbound_notifier,
                               &input_capacity_notifier);
  workers.start(1);

  ASSERT_TRUE(
      waitUntil([&] { return outbound_notifier.notifications.load() == 3; }));
  workers.beginStop();
  workers.join();

  EXPECT_EQ(input_capacity_notifier.notifications.load(), 1);
  EXPECT_EQ(outbound_notifier.notifications.load(), 3);
}

TEST(WorkerPoolNotificationTest, ClosingFullOutboxReleasesBlockedWorker) {
  using rss::service::OutboundMessage;
  using rss::service::SessionEvent;
  using rss::service::SessionEventKind;

  rss::util::BoundedBlockingQueue<SessionEvent> inbox(1);
  rss::util::BoundedBlockingQueue<OutboundMessage> outbox(1);
  RecordingSessionEventHandler handler;
  ASSERT_TRUE(outbox.push(OutboundMessage{99, {0x99U}}).succeeded);

  rss::net::WorkerPool workers(inbox, outbox, handler, 1);
  workers.start(1);
  ASSERT_TRUE(
      inbox.push(SessionEvent{SessionEventKind::Packet, 42, {}}).succeeded);
  ASSERT_TRUE(waitUntil([&] { return outbox.waiterCounts().producers == 1; }));

  outbox.close();
  std::atomic<bool> joined{false};
  std::thread joiner([&] {
    workers.join();
    joined.store(true);
  });

  const auto joined_before_deadline = waitUntil([&] { return joined.load(); });
  if (!joined_before_deadline) {
    workers.beginStop();
  }
  joiner.join();

  EXPECT_TRUE(joined_before_deadline);
  EXPECT_TRUE(workers.finished());
}

TEST(WorkerPoolNotificationTest, TracksActiveWorkersUntilTheyExit) {
  rss::util::BoundedBlockingQueue<rss::service::SessionEvent> inbox(2);
  rss::util::BoundedBlockingQueue<rss::service::OutboundMessage> outbox(2);
  RecordingSessionEventHandler handler;
  rss::net::WorkerPool workers(inbox, outbox, handler, 1);

  workers.start(2);
  ASSERT_TRUE(waitUntil([&] { return inbox.waiterCounts().consumers == 2; }));
  EXPECT_FALSE(workers.finished());

  workers.beginStop();
  ASSERT_TRUE(waitUntil([&] { return workers.finished(); }));
  workers.join();
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
    rss::net::WorkerPool workers(inbox, outbox, handler, 1);
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
