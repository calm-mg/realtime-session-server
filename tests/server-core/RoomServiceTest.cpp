#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rss/domain/Position.h"
#include "rss/service/RoomService.h"

namespace {

using rss::service::RoomService;

void expectRepeatedLoginInRoomRejected(std::string_view repeated_name) {
  RoomService service;
  const auto login = service.login(100, "alice");
  ASSERT_TRUE(login.ok);
  const auto room = service.createRoom(100, "arena");
  ASSERT_TRUE(room.ok);

  const auto repeated = service.login(100, std::string(repeated_name));

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

  const auto alice = service.login(100, "alice").user;
  const auto bob = service.login(200, "bob").user;

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

TEST(RoomServiceTest, RejectsRepeatedLoginWithoutChangingUser) {
  RoomService service;

  const auto first = service.login(100, "alice");
  const auto repeated_same_name = service.login(100, "alice");
  const auto repeated_different_name = service.login(100, "mallory");

  ASSERT_TRUE(first.ok);
  EXPECT_FALSE(repeated_same_name.ok);
  EXPECT_EQ(repeated_same_name.error, "user is already logged in");
  EXPECT_EQ(repeated_same_name.user.id, first.user.id);
  EXPECT_EQ(repeated_same_name.user.name, "alice");
  EXPECT_FALSE(repeated_different_name.ok);
  EXPECT_EQ(repeated_different_name.error, "user is already logged in");
  EXPECT_EQ(repeated_different_name.user.id, first.user.id);
  EXPECT_EQ(repeated_different_name.user.name, "alice");

  const auto stored = service.userOf(100);
  ASSERT_TRUE(stored.has_value());
  EXPECT_EQ(stored->id, first.user.id);
  EXPECT_EQ(stored->name, "alice");
}

TEST(RoomServiceTest, KeepsRoomActionsOnOriginalUserAfterRepeatedLogin) {
  RoomService service;
  ASSERT_TRUE(service.login(100, "alice").ok);
  ASSERT_TRUE(service.createRoom(100, "first").ok);

  ASSERT_FALSE(service.login(100, "mallory").ok);

  const auto chat = service.chat(100);
  ASSERT_TRUE(chat.ok);
  EXPECT_EQ(chat.actor.name, "alice");
  const auto leave = service.leaveRoom(100);
  ASSERT_TRUE(leave.ok);
  EXPECT_EQ(leave.actor.name, "alice");
}

TEST(RoomServiceTest, RoutesRoomActionsToCurrentMembers) {
  RoomService service;
  service.login(100, "alice");
  service.login(200, "bob");

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

TEST(RoomServiceTest, RejectsRejoiningSameRoomForOnlyMember) {
  RoomService service;
  service.login(100, "alice");

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
  service.login(100, "alice");
  service.login(200, "bob");

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
  ASSERT_TRUE(service.login(100, "alice").ok);
  ASSERT_TRUE(service.login(200, "bob").ok);

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
  ASSERT_TRUE(service.login(100, "alice").ok);
  ASSERT_TRUE(service.login(200, "bob").ok);
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
