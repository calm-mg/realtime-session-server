#include <gtest/gtest.h>

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

Packet decodeSingleMessage(const OutboundMessage& message) {
  PacketCodec codec;
  codec.feed(message.bytes.data(), message.bytes.size());
  auto packets = codec.drainPackets();
  if (packets.size() != 1) {
    throw std::runtime_error("expected exactly one outbound packet");
  }
  return std::move(packets.front());
}

SessionEvent event(std::uint64_t session_id, PacketType type,
                   std::string_view payload) {
  return SessionEvent{SessionEventKind::Packet, session_id,
                      decodeSinglePacket(type, payload)};
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

TEST(MessageRouterTest, RejectsRepeatedLoginWithoutChangingUser) {
  RoomService service;
  MessageRouter router(service);

  ASSERT_EQ(route(router, event(1, PacketType::LoginReq, "alice")).size(), 1);
  const auto original_user = service.userOf(1);
  ASSERT_TRUE(original_user.has_value());

  const auto output = route(router, event(1, PacketType::LoginReq, "mallory"));

  ASSERT_EQ(output.size(), 1);
  EXPECT_EQ(output.front().session_id, 1);
  const auto packet = decodeSingleMessage(output.front());
  EXPECT_EQ(packet.type, PacketType::Error);
  EXPECT_EQ(rss::protocol::payloadToString(packet),
            "user is already logged in");

  const auto current_user = service.userOf(1);
  ASSERT_TRUE(current_user.has_value());
  EXPECT_EQ(current_user->id, original_user->id);
  EXPECT_EQ(current_user->name, "alice");
}

TEST(MessageRouterTest, RejectsJoiningAnotherRoomWithoutBroadcasting) {
  RoomService service;
  MessageRouter router(service);

  ASSERT_EQ(route(router, event(1, PacketType::LoginReq, "alice")).size(), 1);
  ASSERT_EQ(route(router, event(1, PacketType::CreateRoomReq, "arena")).size(),
            1);
  ASSERT_EQ(route(router, event(2, PacketType::LoginReq, "bob")).size(), 1);
  ASSERT_EQ(route(router, event(2, PacketType::CreateRoomReq, "other")).size(),
            1);

  const auto output = route(router, event(1, PacketType::JoinRoomReq, "2"));

  ASSERT_EQ(output.size(), 1);
  EXPECT_EQ(output.front().session_id, 1);
  const auto packet = decodeSingleMessage(output.front());
  EXPECT_EQ(packet.type, PacketType::Error);
  EXPECT_EQ(rss::protocol::payloadToString(packet), "leave current room first");

  const auto alice_chat = service.chat(1);
  ASSERT_TRUE(alice_chat.ok);
  EXPECT_EQ(alice_chat.room_id, 1);
  EXPECT_EQ(alice_chat.recipients, std::vector<std::uint64_t>{1});

  const auto bob_chat = service.chat(2);
  ASSERT_TRUE(bob_chat.ok);
  EXPECT_EQ(bob_chat.room_id, 2);
  EXPECT_EQ(bob_chat.recipients, std::vector<std::uint64_t>{2});
}

TEST(MessageRouterTest, RejectsRoomCreationWhileInRoomWithoutBroadcasting) {
  RoomService service;
  MessageRouter router(service);

  ASSERT_EQ(route(router, event(1, PacketType::LoginReq, "alice")).size(), 1);
  ASSERT_EQ(route(router, event(1, PacketType::CreateRoomReq, "arena")).size(),
            1);
  ASSERT_EQ(route(router, event(2, PacketType::LoginReq, "bob")).size(), 1);
  ASSERT_EQ(route(router, event(2, PacketType::JoinRoomReq, "1")).size(), 2);

  const auto output =
      route(router, event(1, PacketType::CreateRoomReq, "other"));

  ASSERT_EQ(output.size(), 1);
  EXPECT_EQ(output.front().session_id, 1);
  const auto packet = decodeSingleMessage(output.front());
  EXPECT_EQ(packet.type, PacketType::Error);
  EXPECT_EQ(rss::protocol::payloadToString(packet), "leave current room first");

  const auto chat = service.chat(1);
  ASSERT_TRUE(chat.ok);
  EXPECT_EQ(chat.room_id, 1);
  EXPECT_EQ(chat.recipients.size(), 2);
}

TEST(MessageRouterTest, LeavesThenJoinsAnotherRoomWithOrderedMessages) {
  RoomService service;
  MessageRouter router(service);

  ASSERT_EQ(route(router, event(1, PacketType::LoginReq, "alice")).size(), 1);
  ASSERT_EQ(route(router, event(1, PacketType::CreateRoomReq, "arena")).size(),
            1);
  ASSERT_EQ(route(router, event(2, PacketType::LoginReq, "observer")).size(),
            1);
  ASSERT_EQ(route(router, event(2, PacketType::JoinRoomReq, "1")).size(), 2);
  ASSERT_EQ(route(router, event(3, PacketType::LoginReq, "owner")).size(), 1);
  ASSERT_EQ(route(router, event(3, PacketType::CreateRoomReq, "other")).size(),
            1);
  ASSERT_EQ(route(router, event(1, PacketType::JoinRoomReq, "2")).size(), 1);

  const auto leave = route(router, event(1, PacketType::LeaveRoomReq, ""));

  ASSERT_EQ(leave.size(), 2);
  EXPECT_EQ(leave[0].session_id, 1);
  const auto leave_response = decodeSingleMessage(leave[0]);
  EXPECT_EQ(leave_response.type, PacketType::LeaveRoomRes);
  EXPECT_EQ(rss::protocol::payloadToString(leave_response),
            "OK|event=LEAVE_ROOM|room_id=1|user_id=1|session_id=1|name=alice");
  EXPECT_EQ(leave[1].session_id, 2);
  const auto leave_broadcast = decodeSingleMessage(leave[1]);
  EXPECT_EQ(leave_broadcast.type, PacketType::RoomBroadcast);
  EXPECT_EQ(rss::protocol::payloadToString(leave_broadcast),
            "OK|event=LEAVE|room_id=1|user_id=1|session_id=1|name=alice");

  const auto join = route(router, event(1, PacketType::JoinRoomReq, "2"));

  ASSERT_EQ(join.size(), 2);
  EXPECT_EQ(join[0].session_id, 1);
  const auto join_response = decodeSingleMessage(join[0]);
  EXPECT_EQ(join_response.type, PacketType::JoinRoomRes);
  EXPECT_EQ(rss::protocol::payloadToString(join_response),
            "OK|event=JOIN_ROOM|room_id=2|user_id=1|session_id=1|name=alice");
  EXPECT_EQ(join[1].session_id, 3);
  const auto join_broadcast = decodeSingleMessage(join[1]);
  EXPECT_EQ(join_broadcast.type, PacketType::RoomBroadcast);
  EXPECT_EQ(rss::protocol::payloadToString(join_broadcast),
            "OK|event=JOIN|room_id=2|user_id=1|session_id=1|name=alice");
}

}  // namespace
