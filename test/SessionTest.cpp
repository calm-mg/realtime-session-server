#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "rss/net/Session.h"

namespace {

TEST(SessionTest, AccountsPartialAndCompletedWrites) {
  rss::net::Session session(7, 42, 10);
  ASSERT_TRUE(session.tryEnqueue({1, 2, 3, 4}));
  ASSERT_TRUE(session.tryEnqueue({5, 6, 7}));
  EXPECT_EQ(session.pendingWriteBytes(), 7U);

  session.consumeWrite(2);
  EXPECT_EQ(session.pendingWriteBytes(), 5U);

  session.consumeWrite(2);
  EXPECT_EQ(session.pendingWriteBytes(), 3U);
}

TEST(SessionTest, RejectsWritesAboveThePendingByteLimitWithoutChangingState) {
  rss::net::Session session(7, 42, 5);
  ASSERT_TRUE(session.tryEnqueue({1, 2, 3}));
  ASSERT_TRUE(session.tryEnqueue({4, 5}));

  EXPECT_FALSE(session.tryEnqueue({6}));
  EXPECT_EQ(session.pendingWriteBytes(), 5U);

  session.consumeWrite(3);
  EXPECT_EQ(session.pendingWriteBytes(), 2U);
  ASSERT_TRUE(session.hasPendingWrite());
  EXPECT_EQ(session.currentWrite().bytes,
            (std::vector<std::uint8_t>{4, 5}));
  EXPECT_EQ(session.currentWrite().offset, 0U);

  EXPECT_TRUE(session.tryEnqueue({6, 7, 8}));
  EXPECT_EQ(session.pendingWriteBytes(), 5U);
}

TEST(SessionTest, RejectsInvalidWriteConsumption) {
  rss::net::Session session(7, 42, 10);

  EXPECT_THROW(session.consumeWrite(1), std::logic_error);

  ASSERT_TRUE(session.tryEnqueue({1, 2, 3}));
  EXPECT_THROW(session.consumeWrite(4), std::logic_error);
  EXPECT_EQ(session.pendingWriteBytes(), 3U);
}

}  // namespace
