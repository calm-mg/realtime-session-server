#include "TestSupport.h"

#include "rss/net/CompletionNotifier.h"
#include "rss/net/WorkerPool.h"
#include "rss/protocol/PacketCodec.h"
#include "rss/service/MessageRouter.h"

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <utility>

namespace {

class RecordingNotifier final : public rss::net::CompletionNotifier {
public:
    void notify() noexcept override
    {
        notifications.fetch_add(1);
    }

    std::atomic<int> notifications{0};
};

rss::protocol::Packet packet(rss::protocol::PacketType type, std::string_view payload)
{
    auto bytes = rss::protocol::PacketCodec::encode(type, payload);
    rss::protocol::PacketCodec codec;
    codec.feed(bytes.data(), bytes.size());
    auto packets = codec.drainPackets();
    RSS_EXPECT(packets.size() == 1);
    return std::move(packets[0]);
}

std::optional<rss::service::OutboundMessage>
waitForOutbound(rss::util::BlockingQueue<rss::service::OutboundMessage>& outbox)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto message = outbox.tryPop()) {
            return message;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

} // namespace

int main()
{
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
    RSS_EXPECT(inbox.push(SessionEvent{SessionEventKind::Packet, 1, packet(PacketType::Ping, "")}));

    auto message = waitForOutbound(outbox);
    workers.stop();

    RSS_EXPECT(message.has_value());
    RSS_EXPECT(notifier.notifications.load() == 1);

    testPassed("WorkerPoolNotificationTest");
    return 0;
}
