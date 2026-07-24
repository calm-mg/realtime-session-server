#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

#include "rss/net/CompletionNotifier.h"
#include "rss/net/WorkerPool.h"
#include "rss/protocol/PacketCodec.h"
#include "rss/service/MessageRouter.h"

namespace {

class RecordingNotifier final : public rss::net::CompletionNotifier {
 public:
  void notify() noexcept override { notifications.fetch_add(1); }

  std::atomic<int> notifications{0};
};

rss::protocol::Packet decodeSinglePacket(rss::protocol::PacketType type,
                                         std::string_view payload) {
  const auto bytes = rss::protocol::PacketCodec::encode(type, payload);
  rss::protocol::PacketCodec codec;
  codec.feed(bytes.data(), bytes.size());
  auto packets = codec.drainPackets();
  if (packets.size() != 1) {
    throw std::runtime_error("expected exactly one decoded packet");
  }
  return std::move(packets.front());
}

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

TEST(WorkerPoolNotificationTest, NotifiesWhenOutboundMessageIsReady) {
  using rss::protocol::PacketType;
  using rss::service::MessageRouter;
  using rss::service::RoomService;
  using rss::service::SessionEvent;
  using rss::service::SessionEventKind;

  rss::util::BlockingQueue<SessionEvent> inbox;
  rss::util::BlockingQueue<rss::service::OutboundMessage> outbox;
  RoomService service;
  MessageRouter router(service);
  RecordingNotifier notifier;
  rss::net::WorkerPool workers(inbox, outbox, router, &notifier);

  workers.start(1);
  ASSERT_TRUE(inbox.push(SessionEvent{
      SessionEventKind::Packet, 1, decodeSinglePacket(PacketType::Ping, "")}));

  const auto message = waitForOutbound(outbox);
  workers.stop();

  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(notifier.notifications.load(), 1);
}

}  // namespace
