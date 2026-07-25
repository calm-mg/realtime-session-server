#include <gtest/gtest.h>

#include <cstdint>

#include "rss/protocol/PacketCodec.h"

namespace {

using rss::protocol::PacketCodec;
using rss::protocol::PacketType;
using rss::protocol::ProtocolError;

TEST(PacketCodecTest, DecodesPacketSplitAcrossReads) {
  const auto login = PacketCodec::encode(PacketType::LoginReq, "alice");
  ASSERT_EQ(login.size(), 9);

  PacketCodec codec;
  codec.feed(login.data(), 2);
  EXPECT_TRUE(codec.drainPackets().empty());

  codec.feed(login.data() + 2, login.size() - 2);
  const auto packets = codec.drainPackets();

  ASSERT_EQ(packets.size(), 1);
  EXPECT_EQ(packets[0].type, PacketType::LoginReq);
  EXPECT_EQ(rss::protocol::payloadToString(packets[0]), "alice");
}

TEST(PacketCodecTest, KeepsCompletedPacketUntilExplicitlyConsumed) {
  const auto first = PacketCodec::encode(PacketType::Ping, "one");
  const auto second = PacketCodec::encode(PacketType::Ping, "two");
  std::vector<std::uint8_t> bytes(first);
  bytes.insert(bytes.end(), second.begin(), second.end());

  PacketCodec codec;
  codec.feed(bytes.data(), bytes.size());
  EXPECT_EQ(rss::protocol::payloadToString(*codec.peekPacket()), "one");
  EXPECT_EQ(rss::protocol::payloadToString(*codec.peekPacket()), "one");
  codec.consumePacket();
  EXPECT_EQ(rss::protocol::payloadToString(*codec.peekPacket()), "two");
  codec.consumePacket();
  EXPECT_FALSE(codec.peekPacket().has_value());
}

TEST(PacketCodecTest, RejectsConsumingWhenNoCompletedPacketExists) {
  PacketCodec codec;

  EXPECT_THROW(codec.consumePacket(), std::logic_error);
}

TEST(PacketCodecTest, RoundTripsPositionPayload) {
  const auto position = PacketCodec::encodePosition(10.5F, -3.25F);
  const auto [x, y] = PacketCodec::decodePosition(position);

  EXPECT_FLOAT_EQ(x, 10.5F);
  EXPECT_FLOAT_EQ(y, -3.25F);
}

TEST(PacketCodecTest, RejectsPacketSmallerThanHeader) {
  const std::uint8_t invalid[] = {0, 3, 0, 1};
  PacketCodec codec;
  codec.feed(invalid, sizeof(invalid));

  EXPECT_THROW((void)codec.drainPackets(), ProtocolError);
}

}  // namespace
