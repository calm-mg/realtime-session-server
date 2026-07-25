#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <utility>

#include "rss/net/CompletionNotifier.h"
#include "rss/net/WorkerPool.h"
#include "rss/service/SessionEventHandler.h"

namespace {

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

std::optional<rss::service::OutboundMessage> waitForOutbound(
    rss::util::BlockingQueue<rss::service::OutboundMessage>& outbox) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto message = outbox.tryPop()) {
      return message;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return std::nullopt;
}

TEST(WorkerPoolNotificationTest, ProcessesEventsThroughSessionEventHandler) {
  using rss::service::SessionEvent;
  using rss::service::SessionEventKind;

  rss::util::BlockingQueue<SessionEvent> inbox;
  rss::util::BlockingQueue<rss::service::OutboundMessage> outbox;
  RecordingSessionEventHandler handler;
  RecordingNotifier notifier;
  rss::net::WorkerPool workers(inbox, outbox, handler, &notifier);

  workers.start(1);
  ASSERT_TRUE(inbox.push(SessionEvent{SessionEventKind::Packet, 42, {}}));

  const auto message = waitForOutbound(outbox);
  workers.stop();

  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(handler.handled_session_id.load(), 42);
  EXPECT_EQ(message->session_id, 42);
  EXPECT_EQ(message->bytes, (std::vector<std::uint8_t>{0x01U}));
  EXPECT_EQ(notifier.notifications.load(), 1);
}

}  // namespace
