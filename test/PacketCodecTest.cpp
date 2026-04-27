#include "TestSupport.h"

#include "rss/protocol/PacketCodec.h"

#include <cstring>

int main()
{
    using rss::protocol::PacketCodec;
    using rss::protocol::PacketType;
    using rss::protocol::ProtocolError;

    const auto login = PacketCodec::encode(PacketType::LoginReq, "alice");
    RSS_EXPECT(login.size() == 9);

    PacketCodec codec;
    codec.feed(login.data(), 2);
    RSS_EXPECT(codec.drainPackets().empty());
    codec.feed(login.data() + 2, login.size() - 2);

    auto packets = codec.drainPackets();
    RSS_EXPECT(packets.size() == 1);
    RSS_EXPECT(packets[0].type == PacketType::LoginReq);
    RSS_EXPECT(rss::protocol::payloadToString(packets[0]) == "alice");

    const auto position = PacketCodec::encodePosition(10.5F, -3.25F);
    const auto [x, y] = PacketCodec::decodePosition(position);
    RSS_EXPECT(x == 10.5F);
    RSS_EXPECT(y == -3.25F);

    bool threw = false;
    try {
        std::uint8_t invalid[] = {0, 3, 0, 1};
        PacketCodec bad;
        bad.feed(invalid, sizeof(invalid));
        (void)bad.drainPackets();
    } catch (const ProtocolError&) {
        threw = true;
    }
    RSS_EXPECT(threw);

    testPassed("PacketCodecTest");
    return 0;
}
