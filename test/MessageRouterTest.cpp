#include "TestSupport.h"

#include "rss/protocol/PacketCodec.h"
#include "rss/service/MessageRouter.h"

#include <utility>

namespace {

rss::protocol::Packet packet(rss::protocol::PacketType type, std::string_view payload)
{
    auto bytes = rss::protocol::PacketCodec::encode(type, payload);
    rss::protocol::PacketCodec codec;
    codec.feed(bytes.data(), bytes.size());
    auto packets = codec.drainPackets();
    RSS_EXPECT(packets.size() == 1);
    return std::move(packets[0]);
}

} // namespace

int main()
{
    using rss::protocol::PacketCodec;
    using rss::protocol::PacketType;
    using rss::service::MessageRouter;
    using rss::service::RoomService;
    using rss::service::SessionEvent;
    using rss::service::SessionEventKind;

    RoomService service;
    MessageRouter router(service);

    auto out = router.handle(SessionEvent{SessionEventKind::Packet, 1, packet(PacketType::LoginReq, "alice")});
    RSS_EXPECT(out.size() == 1);

    out = router.handle(SessionEvent{SessionEventKind::Packet, 1, packet(PacketType::CreateRoomReq, "arena")});
    RSS_EXPECT(out.size() == 1);

    out = router.handle(SessionEvent{SessionEventKind::Packet, 2, packet(PacketType::LoginReq, "bob")});
    RSS_EXPECT(out.size() == 1);

    out = router.handle(SessionEvent{SessionEventKind::Packet, 2, packet(PacketType::JoinRoomReq, "1")});
    RSS_EXPECT(out.size() == 2);

    out = router.handle(SessionEvent{SessionEventKind::Packet, 1, packet(PacketType::ChatReq, "hello")});
    RSS_EXPECT(out.size() == 2);

    const auto position_payload = PacketCodec::encodePosition(1.0F, 2.0F);
    out = router.handle(SessionEvent{
        SessionEventKind::Packet,
        2,
        rss::protocol::Packet{PacketType::PositionUpdate, position_payload},
    });
    RSS_EXPECT(out.size() == 2);

    out = router.handle(SessionEvent{SessionEventKind::Packet, 1, packet(PacketType::Ping, "")});
    RSS_EXPECT(out.size() == 1);

    testPassed("MessageRouterTest");
    return 0;
}
