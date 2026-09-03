#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rss/domain/Position.h"
#include "rss/persistence/UserRecord.h"
#include "rss/protocol/Packet.h"
#include "rss/service/RoomService.h"

namespace {

using rss::service::RoomService;

rss::persistence::UserRecord userRecord(std::uint64_t id, std::string name) {
  rss::domain::UserId::Bytes bytes{};
  for (std::size_t index = 0; index < sizeof(id); ++index) {
    bytes[bytes.size() - 1 - index] = static_cast<std::uint8_t>(id & 0xffU);
    id >>= 8U;
  }
  return {rss::domain::UserId{bytes}, name, std::move(name)};
}

rss::service::LoginResult attach(RoomService& service, std::uint64_t session_id,
                                 std::uint64_t user_id, std::string name) {
  return service.attachUser(session_id, userRecord(user_id, std::move(name)));
}

void expectRepeatedLoginInRoomRejected(std::string_view repeated_name) {
  RoomService service;
  const auto login = attach(service, 100, 1, "alice");
  ASSERT_TRUE(login.ok);
  const auto room = service.createRoom(100, "arena");
  ASSERT_TRUE(room.ok);

  const auto repeated = attach(service, 100, 2, std::string(repeated_name));

  EXPECT_FALSE(repeated.ok);
  EXPECT_EQ(repeated.error, "user is already logged in");
  EXPECT_EQ(repeated.user.id, login.user.id);
  EXPECT_EQ(repeated.user.name, "alice");

  const auto chat = service.chat(100);
  ASSERT_TRUE(chat.ok);
  EXPECT_EQ(chat.room_id, room.room_id);
  EXPECT_EQ(chat.actor.id, login.user.id);
  EXPECT_EQ(chat.actor.name, "alice");

  const auto leave = service.leaveRoom(100);
  ASSERT_TRUE(leave.ok);
  EXPECT_EQ(leave.room_id, room.room_id);
  EXPECT_EQ(leave.actor.id, login.user.id);
  EXPECT_EQ(leave.actor.name, "alice");
}

TEST(RoomServiceTest, AssignsUniqueUserIds) {
  RoomService service;

  const auto alice = attach(service, 100, 1, "alice");
  const auto bob = attach(service, 200, 2, "bob");

  ASSERT_TRUE(alice.ok);
  ASSERT_TRUE(bob.ok);
  EXPECT_NE(alice.user.id, bob.user.id);
}

TEST(RoomServiceTest, RejectsRepeatedLoginWithSameNameWhileInRoom) {
  expectRepeatedLoginInRoomRejected("alice");
}

TEST(RoomServiceTest, RejectsRepeatedLoginWithDifferentNameWhileInRoom) {
  expectRepeatedLoginInRoomRejected("mallory");
}

TEST(RoomServiceTest, RoutesRoomActionsToCurrentMembers) {
  RoomService service;
  attach(service, 100, 1, "alice");
  attach(service, 200, 2, "bob");

  const auto create = service.createRoom(100, "arena");
  ASSERT_TRUE(create.ok);
  EXPECT_EQ(create.room_id, 1);
  EXPECT_EQ(create.recipients.size(), 1);

  const auto join = service.joinRoom(200, create.room_id);
  ASSERT_TRUE(join.ok);
  EXPECT_EQ(join.recipients.size(), 2);

  const auto chat = service.chat(100);
  ASSERT_TRUE(chat.ok);
  EXPECT_EQ(chat.recipients.size(), 2);

  const auto position =
      service.updatePosition(200, rss::domain::Position{7.0F, 9.0F});
  ASSERT_TRUE(position.ok);
  EXPECT_EQ(position.recipients.size(), 2);

  const auto leave = service.leaveRoom(100);
  ASSERT_TRUE(leave.ok);
  EXPECT_EQ(leave.recipients.size(), 2);

  EXPECT_TRUE(service.disconnect(200).ok);
}

TEST(RoomServiceTest, RejectsInvalidRoomNamesWithoutCreatingRoom) {
  RoomService service;
  ASSERT_TRUE(attach(service, 100, 1, "alice").ok);

  const std::string invalid_names[] = {
      std::string{},
      std::string{" \t"},
      std::string(rss::protocol::kMaxRoomNameBytes + 1, 'x'),
      std::string("\xC0\xAF", 2),
      std::string{"bad\nname"},
  };
  for (const auto& name : invalid_names) {
    const auto rejected = service.createRoom(100, name);
    EXPECT_FALSE(rejected.ok);
    EXPECT_EQ(rejected.error, "invalid room name");
  }

  const auto created = service.createRoom(100, " arena ");
  ASSERT_TRUE(created.ok);
  EXPECT_EQ(created.room_id, 1U);
}

TEST(RoomServiceTest, RejectsRejoiningSameRoomForOnlyMember) {
  RoomService service;
  attach(service, 100, 1, "alice");

  const auto create = service.createRoom(100, "arena");
  ASSERT_TRUE(create.ok);

  const auto rejoin = service.joinRoom(100, create.room_id);
  EXPECT_FALSE(rejoin.ok);
  EXPECT_EQ(rejoin.error, "user is already in room");

  const auto chat = service.chat(100);
  ASSERT_TRUE(chat.ok);
  EXPECT_EQ(chat.room_id, create.room_id);
  EXPECT_EQ(chat.recipients, std::vector<std::uint64_t>{100});

  const auto leave = service.leaveRoom(100);
  EXPECT_TRUE(leave.ok);
  EXPECT_EQ(leave.room_id, create.room_id);
}

TEST(RoomServiceTest, RejectsRejoiningSameRoomWithoutChangingMembers) {
  RoomService service;
  attach(service, 100, 1, "alice");
  attach(service, 200, 2, "bob");

  const auto create = service.createRoom(100, "arena");
  ASSERT_TRUE(create.ok);
  ASSERT_TRUE(service.joinRoom(200, create.room_id).ok);

  const auto rejoin = service.joinRoom(200, create.room_id);
  EXPECT_FALSE(rejoin.ok);
  EXPECT_EQ(rejoin.error, "user is already in room");

  const auto chat = service.chat(200);
  ASSERT_TRUE(chat.ok);
  EXPECT_EQ(chat.recipients.size(), 2);

  const auto leave = service.leaveRoom(200);
  EXPECT_TRUE(leave.ok);
  EXPECT_EQ(leave.recipients.size(), 2);

  const auto remaining_member_chat = service.chat(100);
  ASSERT_TRUE(remaining_member_chat.ok);
  EXPECT_EQ(remaining_member_chat.recipients, std::vector<std::uint64_t>{100});
}

TEST(RoomServiceTest, RejectsRoomCreationWhileInRoomWithoutChangingState) {
  RoomService service;
  ASSERT_TRUE(attach(service, 100, 1, "alice").ok);
  ASSERT_TRUE(attach(service, 200, 2, "bob").ok);

  const auto original_room = service.createRoom(100, "arena");
  ASSERT_TRUE(original_room.ok);
  ASSERT_TRUE(service.joinRoom(200, original_room.room_id).ok);

  const auto rejected = service.createRoom(100, "other");

  EXPECT_FALSE(rejected.ok);
  EXPECT_EQ(rejected.error, "leave current room first");
  const auto chat = service.chat(100);
  ASSERT_TRUE(chat.ok);
  EXPECT_EQ(chat.room_id, original_room.room_id);
  EXPECT_EQ(chat.recipients.size(), 2);

  ASSERT_TRUE(service.leaveRoom(100).ok);
  const auto next_room = service.createRoom(100, "other");
  ASSERT_TRUE(next_room.ok);
  EXPECT_EQ(next_room.room_id, original_room.room_id + 1);
}

TEST(RoomServiceTest, ChecksCurrentRoomBeforeTargetRoomExists) {
  RoomService service;
  ASSERT_TRUE(attach(service, 100, 1, "alice").ok);
  ASSERT_TRUE(attach(service, 200, 2, "bob").ok);
  const auto original_room = service.createRoom(100, "arena");
  ASSERT_TRUE(original_room.ok);
  const auto target_room = service.createRoom(200, "other");
  ASSERT_TRUE(target_room.ok);

  const auto rejected = service.joinRoom(100, 999);

  EXPECT_FALSE(rejected.ok);
  EXPECT_EQ(rejected.error, "leave current room first");
  const auto chat = service.chat(100);
  ASSERT_TRUE(chat.ok);
  EXPECT_EQ(chat.room_id, original_room.room_id);

  ASSERT_TRUE(service.leaveRoom(100).ok);
  const auto joined = service.joinRoom(100, target_room.room_id);
  ASSERT_TRUE(joined.ok);
  EXPECT_EQ(joined.room_id, target_room.room_id);
  EXPECT_EQ(joined.recipients.size(), 2);
}

}  // namespace
