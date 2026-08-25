#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "rss/domain/Position.h"
#include "rss/service/RoomService.h"

namespace {

using rss::service::RoomService;

TEST(RoomServiceTest, AssignsUniqueUserIds) {
  RoomService service;

  const auto alice = service.login(100, "alice").user;
  const auto bob = service.login(200, "bob").user;

  EXPECT_NE(alice.id, bob.id);
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

TEST(RoomServiceTest, RejectsCreatingRoomUntilCurrentRoomIsLeft) {
  RoomService service;
  service.login(100, "alice");
  service.login(200, "bob");

  const auto first_room = service.createRoom(100, "first");
  ASSERT_TRUE(first_room.ok);
  const auto second_room = service.createRoom(200, "second");
  ASSERT_TRUE(second_room.ok);

  const auto rejected = service.createRoom(100, "forbidden");
  EXPECT_FALSE(rejected.ok);
  EXPECT_EQ(rejected.error, "leave current room first");

  const auto chat = service.chat(100);
  ASSERT_TRUE(chat.ok);
  EXPECT_EQ(chat.room_id, first_room.room_id);
  EXPECT_EQ(chat.recipients, std::vector<std::uint64_t>{100});

  ASSERT_TRUE(service.leaveRoom(100).ok);
  const auto created_after_leave = service.createRoom(100, "allowed");
  ASSERT_TRUE(created_after_leave.ok);
  EXPECT_EQ(created_after_leave.room_id, second_room.room_id + 1);
}

TEST(RoomServiceTest, RejectsJoiningAnotherRoomUntilCurrentRoomIsLeft) {
  RoomService service;
  service.login(100, "alice");
  service.login(200, "bob");

  const auto first_room = service.createRoom(100, "first");
  ASSERT_TRUE(first_room.ok);
  const auto second_room = service.createRoom(200, "second");
  ASSERT_TRUE(second_room.ok);

  const auto rejected = service.joinRoom(100, second_room.room_id);
  EXPECT_FALSE(rejected.ok);
  EXPECT_EQ(rejected.error, "leave current room first");

  const auto chat = service.chat(100);
  ASSERT_TRUE(chat.ok);
  EXPECT_EQ(chat.room_id, first_room.room_id);
  EXPECT_EQ(chat.recipients, std::vector<std::uint64_t>{100});

  ASSERT_TRUE(service.leaveRoom(100).ok);
  const auto joined_after_leave = service.joinRoom(100, second_room.room_id);
  ASSERT_TRUE(joined_after_leave.ok);
  EXPECT_EQ(joined_after_leave.recipients.size(), 2);
  EXPECT_NE(std::find(joined_after_leave.recipients.begin(),
                      joined_after_leave.recipients.end(), 100),
            joined_after_leave.recipients.end());
  EXPECT_NE(std::find(joined_after_leave.recipients.begin(),
                      joined_after_leave.recipients.end(), 200),
            joined_after_leave.recipients.end());
}

TEST(RoomServiceTest, RequiresLeavingBeforeLookingUpAnotherRoom) {
  RoomService service;
  service.login(100, "alice");
  ASSERT_TRUE(service.createRoom(100, "first").ok);

  const auto rejected = service.joinRoom(100, 999);

  EXPECT_FALSE(rejected.ok);
  EXPECT_EQ(rejected.error, "leave current room first");
  EXPECT_TRUE(service.chat(100).ok);
}

}  // namespace
