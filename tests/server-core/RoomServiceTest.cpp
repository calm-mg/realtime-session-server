#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "rss/domain/Position.h"
#include "rss/service/RoomService.h"

namespace {

using rss::service::RoomService;

TEST(RoomServiceTest, AssignsUniqueUserIds) {
  RoomService service;

  const auto alice = service.login(100, "alice");
  const auto bob = service.login(200, "bob");

  EXPECT_NE(alice.id, bob.id);
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

}  // namespace
