#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "rss/protocol/PacketCodec.h"
#include "rss/service/MessageRouter.h"

namespace {

using rss::protocol::Packet;
using rss::protocol::PacketCodec;
using rss::protocol::PacketType;
using rss::service::MessageRouter;
using rss::service::OutboundMessage;
using rss::service::OutboundMessageSink;
using rss::service::RoomService;
using rss::service::SessionEvent;
using rss::service::SessionEventKind;

Packet decodeSinglePacket(PacketType type, std::string_view payload) {
  const auto bytes = PacketCodec::encode(type, payload);
  PacketCodec codec;
  codec.feed(bytes.data(), bytes.size());
  auto packets = codec.drainPackets();
  if (packets.size() != 1) {
    throw std::runtime_error("expected exactly one decoded packet");
  }
  return std::move(packets.front());
}

SessionEvent event(std::uint64_t session_id, PacketType type,
                   std::string_view payload) {
  return SessionEvent{SessionEventKind::Packet, session_id,
                      decodeSinglePacket(type, payload)};
}

Packet decodeMessage(const OutboundMessage& message) {
  PacketCodec codec;
  codec.feed(message.bytes.data(), message.bytes.size());
  auto packets = codec.drainPackets();
  if (packets.size() != 1) {
    throw std::runtime_error("expected exactly one outbound packet");
  }
  return std::move(packets.front());
}

class CollectingSink final : public OutboundMessageSink {
 public:
  bool emit(OutboundMessage message) override {
    messages.push_back(std::move(message));
    return true;
  }

  std::vector<OutboundMessage> messages;
};

std::vector<OutboundMessage> route(MessageRouter& router,
                                   const SessionEvent& input) {
  CollectingSink sink;
  router.handle(input, sink);
  return std::move(sink.messages);
}

TEST(MessageRouterTest, RoutesRoomMessagesToMembers) {
  RoomService service;
  MessageRouter router(service);

  EXPECT_EQ(route(router, event(1, PacketType::LoginReq, "alice")).size(), 1);
  EXPECT_EQ(route(router, event(1, PacketType::CreateRoomReq, "arena")).size(),
            1);
  EXPECT_EQ(route(router, event(2, PacketType::LoginReq, "bob")).size(), 1);
  EXPECT_EQ(route(router, event(2, PacketType::JoinRoomReq, "1")).size(), 2);
  EXPECT_EQ(route(router, event(1, PacketType::ChatReq, "hello")).size(), 2);

  const auto position_payload = PacketCodec::encodePosition(1.0F, 2.0F);
  const auto position =
      route(router, SessionEvent{
                        SessionEventKind::Packet,
                        2,
                        Packet{PacketType::PositionUpdate, position_payload},
                    });
  EXPECT_EQ(position.size(), 2);
}

TEST(MessageRouterTest, RespondsToPing) {
  RoomService service;
  MessageRouter router(service);

  const auto output = route(router, event(1, PacketType::Ping, ""));

  ASSERT_EQ(output.size(), 1);
  PacketCodec codec;
  codec.feed(output.front().bytes.data(), output.front().bytes.size());
  const auto packets = codec.drainPackets();
  ASSERT_EQ(packets.size(), 1);
  EXPECT_EQ(packets.front().type, PacketType::Pong);
  EXPECT_EQ(rss::protocol::payloadToString(packets.front()), "PONG");
}

TEST(MessageRouterTest, ReturnsErrorForRepeatedLogin) {
  RoomService service;
  MessageRouter router(service);
  ASSERT_EQ(route(router, event(1, PacketType::LoginReq, "alice")).size(), 1);

  const auto output = route(router, event(1, PacketType::LoginReq, "mallory"));

  ASSERT_EQ(output.size(), 1);
  EXPECT_EQ(output.front().session_id, 1);
  const auto packet = decodeMessage(output.front());
  EXPECT_EQ(packet.type, PacketType::Error);
  EXPECT_EQ(rss::protocol::payloadToString(packet),
            "user is already logged in");
}

TEST(MessageRouterTest, ReturnsOnlyErrorWhenCreatingRoomBeforeLeaving) {
  RoomService service;
  MessageRouter router(service);
  ASSERT_EQ(route(router, event(1, PacketType::LoginReq, "alice")).size(), 1);
  ASSERT_EQ(route(router, event(1, PacketType::CreateRoomReq, "first")).size(),
            1);

  const auto output =
      route(router, event(1, PacketType::CreateRoomReq, "second"));

  ASSERT_EQ(output.size(), 1);
  EXPECT_EQ(output.front().session_id, 1);
  const auto packet = decodeMessage(output.front());
  EXPECT_EQ(packet.type, PacketType::Error);
  EXPECT_EQ(rss::protocol::payloadToString(packet), "leave current room first");
}

TEST(MessageRouterTest, ReturnsOnlyErrorWhenJoiningRoomBeforeLeaving) {
  RoomService service;
  MessageRouter router(service);
  ASSERT_EQ(route(router, event(1, PacketType::LoginReq, "alice")).size(), 1);
  ASSERT_EQ(route(router, event(1, PacketType::CreateRoomReq, "first")).size(),
            1);
  ASSERT_EQ(route(router, event(4, PacketType::LoginReq, "dave")).size(), 1);
  ASSERT_EQ(route(router, event(4, PacketType::JoinRoomReq, "1")).size(), 2);
  ASSERT_EQ(route(router, event(2, PacketType::LoginReq, "bob")).size(), 1);
  ASSERT_EQ(route(router, event(2, PacketType::CreateRoomReq, "second")).size(),
            1);

  const auto output = route(router, event(1, PacketType::JoinRoomReq, "2"));

  ASSERT_EQ(output.size(), 1);
  EXPECT_EQ(output.front().session_id, 1);
  const auto packet = decodeMessage(output.front());
  EXPECT_EQ(packet.type, PacketType::Error);
  EXPECT_EQ(rss::protocol::payloadToString(packet), "leave current room first");

  const auto chat = route(router, event(1, PacketType::ChatReq, "still here"));
  ASSERT_EQ(chat.size(), 2);
  std::vector<std::uint64_t> chat_recipients{chat[0].session_id,
                                             chat[1].session_id};
  std::sort(chat_recipients.begin(), chat_recipients.end());
  EXPECT_EQ(chat_recipients, (std::vector<std::uint64_t>{1, 4}));
}

TEST(MessageRouterTest, LeavesBeforeJoiningAnotherRoomInResponseOrder) {
  RoomService service;
  MessageRouter router(service);
  ASSERT_EQ(route(router, event(1, PacketType::LoginReq, "alice")).size(), 1);
  ASSERT_EQ(route(router, event(1, PacketType::CreateRoomReq, "first")).size(),
            1);
  ASSERT_EQ(route(router, event(2, PacketType::LoginReq, "bob")).size(), 1);
  ASSERT_EQ(route(router, event(2, PacketType::JoinRoomReq, "1")).size(), 2);
  ASSERT_EQ(route(router, event(3, PacketType::LoginReq, "carol")).size(), 1);
  ASSERT_EQ(route(router, event(3, PacketType::CreateRoomReq, "second")).size(),
            1);

  const auto leave = route(router, event(1, PacketType::LeaveRoomReq, ""));
  ASSERT_EQ(leave.size(), 2);
  EXPECT_EQ(leave[0].session_id, 1);
  EXPECT_EQ(decodeMessage(leave[0]).type, PacketType::LeaveRoomRes);
  EXPECT_EQ(leave[1].session_id, 2);
  EXPECT_EQ(decodeMessage(leave[1]).type, PacketType::RoomBroadcast);

  const auto join = route(router, event(1, PacketType::JoinRoomReq, "2"));
  ASSERT_EQ(join.size(), 2);
  EXPECT_EQ(join[0].session_id, 1);
  EXPECT_EQ(decodeMessage(join[0]).type, PacketType::JoinRoomRes);
  EXPECT_EQ(join[1].session_id, 3);
  EXPECT_EQ(decodeMessage(join[1]).type, PacketType::RoomBroadcast);
}

}  // namespace
