#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "FakeSessionEventContext.h"
#include "rss/persistence/InMemoryUserRepository.h"
#include "rss/protocol/PacketCodec.h"
#include "rss/protocol/StructuredPayload.h"
#include "rss/service/MessageRouter.h"

namespace {

using rss::protocol::Packet;
using rss::protocol::PacketCodec;
using rss::protocol::PacketType;
using rss::service::MessageRouter;
using rss::service::OutboundMessage;
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

std::vector<OutboundMessage> route(MessageRouter& router,
                                   const SessionEvent& input) {
  rss::test::FakeSessionEventContext context;
  router.handle(input, context);
  return context.releaseMessages();
}

class UnavailableUserRepository final
    : public rss::persistence::UserRepository {
 public:
  void findOrCreateByNormalizedName(
      rss::persistence::FindOrCreateUser,
      rss::persistence::UserCallback callback) override {
    callback(
        {std::nullopt, rss::persistence::PersistenceError{
                           rss::persistence::PersistenceErrorKind::Unavailable,
                           "sensitive database detail"}});
  }
};

TEST(MessageRouterTest, RepositoryFailureDoesNotPreventAnotherSessionPing) {
  UnavailableUserRepository users;
  RoomService service;
  MessageRouter router(service, users);

  const auto login = route(router, event(1, PacketType::LoginReq, "alice"));
  ASSERT_EQ(login.size(), 1U);
  const auto login_error = decodeSingleMessage(login.front());
  EXPECT_EQ(login_error.type, PacketType::Error);
  EXPECT_EQ(rss::protocol::payloadToString(login_error),
            "user persistence unavailable");

  const auto ping = route(router, event(2, PacketType::Ping, ""));
  ASSERT_EQ(ping.size(), 1U);
  const auto pong = decodeSingleMessage(ping.front());
  EXPECT_EQ(pong.type, PacketType::Pong);
  EXPECT_EQ(rss::protocol::payloadToString(pong), "PONG");
}

TEST(MessageRouterTest, ReconnectWithSameNameRestoresPermanentUserId) {
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);

  const auto first = route(router, event(1, PacketType::LoginReq, "alice"));
  ASSERT_EQ(first.size(), 1U);
  const auto first_payload =
      rss::protocol::payloadToString(decodeSingleMessage(first.front()));

  EXPECT_TRUE(
      route(router, SessionEvent{SessionEventKind::Disconnected, 1}).empty());

  const auto second = route(router, event(2, PacketType::LoginReq, "alice"));
  ASSERT_EQ(second.size(), 1U);
  const auto second_payload =
      rss::protocol::payloadToString(decodeSingleMessage(second.front()));

  EXPECT_EQ(first_payload,
            "OK|user_id=00000000-0000-0000-0000-000000000001|session_id=1|"
            "name=alice");
  EXPECT_EQ(second_payload,
            "OK|user_id=00000000-0000-0000-0000-000000000001|session_id=2|"
            "name=alice");
}

TEST(MessageRouterTest, TrimsAndFoldsAsciiLoginNameForLookup) {
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);

  const auto first = route(router, event(1, PacketType::LoginReq, " Alice "));
  ASSERT_EQ(first.size(), 1U);
  EXPECT_TRUE(
      route(router, SessionEvent{SessionEventKind::Disconnected, 1}).empty());
  const auto second = route(router, event(2, PacketType::LoginReq, "ALICE"));

  ASSERT_EQ(second.size(), 1U);
  EXPECT_EQ(rss::protocol::payloadToString(decodeSingleMessage(second.front())),
            "OK|user_id=00000000-0000-0000-0000-000000000001|session_id=2|"
            "name=Alice");
}

TEST(MessageRouterTest, RejectsEmptyAndOversizedLoginNames) {
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);

  for (const auto name : {std::string{" \t\n"}, std::string(33, 'a')}) {
    const auto output = route(router, event(1, PacketType::LoginReq, name));
    ASSERT_EQ(output.size(), 1U);
    const auto packet = decodeSingleMessage(output.front());
    EXPECT_EQ(packet.type, PacketType::Error);
    EXPECT_EQ(rss::protocol::payloadToString(packet), "invalid user name");
  }
}

TEST(MessageRouterTest, PreservesUtf8DisplayName) {
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);

  const auto output = route(router, event(1, PacketType::LoginReq, "한글"));

  ASSERT_EQ(output.size(), 1U);
  EXPECT_EQ(rss::protocol::payloadToString(decodeSingleMessage(output.front())),
            "OK|user_id=00000000-0000-0000-0000-000000000001|session_id=1|"
            "name=한글");
}

TEST(MessageRouterTest, RejectsInvalidLoginWithoutMutatingSession) {
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);

  const auto rejected =
      route(router, event(1, PacketType::LoginReq, std::string("\xC0\xAF", 2)));
  ASSERT_EQ(rejected.size(), 1U);
  const auto error = decodeSingleMessage(rejected.front());
  EXPECT_EQ(error.type, PacketType::Error);
  EXPECT_EQ(rss::protocol::payloadToString(error), "invalid user name");
  EXPECT_FALSE(service.userOf(1).has_value());

  const auto accepted = route(router, event(1, PacketType::LoginReq, "alice"));
  ASSERT_EQ(accepted.size(), 1U);
  EXPECT_EQ(decodeSingleMessage(accepted.front()).type, PacketType::LoginRes);
  EXPECT_TRUE(service.userOf(1).has_value());
}

TEST(MessageRouterTest, RejectsInvalidRoomNameWithoutCreatingRoom) {
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);
  ASSERT_EQ(route(router, event(1, PacketType::LoginReq, "alice")).size(), 1U);

  const auto rejected =
      route(router, event(1, PacketType::CreateRoomReq, "bad\nroom"));
  ASSERT_EQ(rejected.size(), 1U);
  EXPECT_EQ(
      rss::protocol::payloadToString(decodeSingleMessage(rejected.front())),
      "invalid room name");

  const auto accepted =
      route(router, event(1, PacketType::CreateRoomReq, " arena "));
  ASSERT_EQ(accepted.size(), 1U);
  const auto parsed = rss::protocol::StructuredPayload::parse(
      rss::protocol::payloadToString(decodeSingleMessage(accepted.front())));
  EXPECT_EQ(parsed.requireField("room_id"), "1");
}

TEST(MessageRouterTest, EncodesStructuredDynamicValues) {
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);
  constexpr std::string_view name = "kim|role=admin%";

  const auto login = route(router, event(1, PacketType::LoginReq, name));
  ASSERT_EQ(login.size(), 1U);
  const auto login_text =
      rss::protocol::payloadToString(decodeSingleMessage(login.front()));
  EXPECT_NE(login_text.find("name=kim%7Crole%3Dadmin%25"), std::string::npos);
  const auto parsed_login = rss::protocol::StructuredPayload::parse(login_text);
  EXPECT_EQ(parsed_login.requireField("name"), name);

  ASSERT_EQ(
      route(router, event(1, PacketType::CreateRoomReq, "room|tier=1%")).size(),
      1U);
  const auto chat =
      route(router, event(1, PacketType::ChatReq, "hello|kind=admin%"));
  ASSERT_EQ(chat.size(), 1U);
  const auto chat_text =
      rss::protocol::payloadToString(decodeSingleMessage(chat.front()));
  EXPECT_NE(chat_text.find("message=hello%7Ckind%3Dadmin%25"),
            std::string::npos);
  const auto parsed_chat = rss::protocol::StructuredPayload::parse(chat_text);
  EXPECT_EQ(parsed_chat.requireField("name"), name);
  EXPECT_EQ(parsed_chat.requireField("message"), "hello|kind=admin%");
}

TEST(MessageRouterTest, PreservesWorstCaseMaximumChatWithinPacketLimit) {
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);
  const std::string reserved_name(rss::protocol::kMaxUserNameBytes, '|');
  const std::string maximum_message(rss::protocol::kMaxChatMessageBytes, '=');

  ASSERT_EQ(route(router, event(1, PacketType::LoginReq, reserved_name)).size(),
            1U);
  ASSERT_EQ(route(router, event(1, PacketType::CreateRoomReq, "arena")).size(),
            1U);
  const auto chat =
      route(router, event(1, PacketType::ChatReq, maximum_message));
  ASSERT_EQ(chat.size(), 1U);
  const auto packet = decodeSingleMessage(chat.front());
  EXPECT_EQ(packet.type, PacketType::RoomBroadcast);
  EXPECT_LE(packet.payload.size() + rss::protocol::kPacketHeaderSize,
            rss::protocol::kMaxPacketSize);

  const auto parsed = rss::protocol::StructuredPayload::parse(
      rss::protocol::payloadToString(packet));
  EXPECT_EQ(parsed.requireField("name"), reserved_name);
  EXPECT_EQ(parsed.requireField("message"), maximum_message);
}

TEST(MessageRouterTest, RejectsInvalidChatWithoutBroadcasting) {
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);
  ASSERT_EQ(route(router, event(1, PacketType::LoginReq, "alice")).size(), 1U);
  ASSERT_EQ(route(router, event(1, PacketType::CreateRoomReq, "arena")).size(),
            1U);

  const auto oversized = route(
      router, event(1, PacketType::ChatReq,
                    std::string(rss::protocol::kMaxChatMessageBytes + 1, 'x')));
  ASSERT_EQ(oversized.size(), 1U);
  const auto oversized_packet = decodeSingleMessage(oversized.front());
  EXPECT_EQ(oversized_packet.type, PacketType::Error);
  EXPECT_EQ(rss::protocol::payloadToString(oversized_packet),
            "chat message too large");

  for (const std::string message :
       {std::string("\xC0\xAF", 2), std::string{"bad\nchat"}}) {
    const auto rejected = route(router, event(1, PacketType::ChatReq, message));
    ASSERT_EQ(rejected.size(), 1U);
    const auto packet = decodeSingleMessage(rejected.front());
    EXPECT_EQ(packet.type, PacketType::Error);
    EXPECT_EQ(rss::protocol::payloadToString(packet), "invalid chat message");
  }
}

TEST(MessageRouterTest, PreservesEmptyAndWhitespaceChat) {
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);
  ASSERT_EQ(route(router, event(1, PacketType::LoginReq, "alice")).size(), 1U);
  ASSERT_EQ(route(router, event(1, PacketType::CreateRoomReq, "arena")).size(),
            1U);

  for (const std::string_view message :
       {std::string_view{}, std::string_view{"   "}}) {
    const auto output = route(router, event(1, PacketType::ChatReq, message));
    ASSERT_EQ(output.size(), 1U);
    const auto parsed = rss::protocol::StructuredPayload::parse(
        rss::protocol::payloadToString(decodeSingleMessage(output.front())));
    EXPECT_EQ(parsed.requireField("message"), message);
  }
}

TEST(MessageRouterTest, RoutesRoomMessagesToMembers) {
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);

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

TEST(MessageRouterTest, EnforcesChatMessageLimitBeforeBroadcasting) {
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);

  ASSERT_EQ(route(router, event(1, PacketType::LoginReq, "alice")).size(), 1);
  ASSERT_EQ(route(router, event(1, PacketType::CreateRoomReq, "arena")).size(),
            1);

  const auto maximum = route(
      router, event(1, PacketType::ChatReq,
                    std::string(rss::protocol::kMaxChatMessageBytes, 'x')));
  ASSERT_EQ(maximum.size(), 1U);
  EXPECT_EQ(decodeSingleMessage(maximum.front()).type,
            PacketType::RoomBroadcast);

  const auto oversized = route(
      router, event(1, PacketType::ChatReq,
                    std::string(rss::protocol::kMaxChatMessageBytes + 1, 'x')));
  ASSERT_EQ(oversized.size(), 1U);
  const auto error = decodeSingleMessage(oversized.front());
  EXPECT_EQ(error.type, PacketType::Error);
  EXPECT_EQ(rss::protocol::payloadToString(error), "chat message too large");
}

TEST(MessageRouterTest, RespondsToPing) {
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);

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
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);

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
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);

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
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);

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
  rss::persistence::InMemoryUserRepository users;
  RoomService service;
  MessageRouter router(service, users);

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
            "OK|event=LEAVE_ROOM|room_id=1|"
            "user_id=00000000-0000-0000-0000-000000000001|session_id=1|"
            "name=alice");
  EXPECT_EQ(leave[1].session_id, 2);
  const auto leave_broadcast = decodeSingleMessage(leave[1]);
  EXPECT_EQ(leave_broadcast.type, PacketType::RoomBroadcast);
  EXPECT_EQ(rss::protocol::payloadToString(leave_broadcast),
            "OK|event=LEAVE|room_id=1|"
            "user_id=00000000-0000-0000-0000-000000000001|session_id=1|"
            "name=alice");

  const auto join = route(router, event(1, PacketType::JoinRoomReq, "2"));

  ASSERT_EQ(join.size(), 2);
  EXPECT_EQ(join[0].session_id, 1);
  const auto join_response = decodeSingleMessage(join[0]);
  EXPECT_EQ(join_response.type, PacketType::JoinRoomRes);
  EXPECT_EQ(rss::protocol::payloadToString(join_response),
            "OK|event=JOIN_ROOM|room_id=2|"
            "user_id=00000000-0000-0000-0000-000000000001|session_id=1|"
            "name=alice");
  EXPECT_EQ(join[1].session_id, 3);
  const auto join_broadcast = decodeSingleMessage(join[1]);
  EXPECT_EQ(join_broadcast.type, PacketType::RoomBroadcast);
  EXPECT_EQ(rss::protocol::payloadToString(join_broadcast),
            "OK|event=JOIN|room_id=2|"
            "user_id=00000000-0000-0000-0000-000000000001|session_id=1|"
            "name=alice");
}

}  // namespace
