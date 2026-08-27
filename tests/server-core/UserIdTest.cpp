#include <gtest/gtest.h>

#include "rss/domain/UserId.h"

namespace rss::domain {
namespace {

TEST(UserIdTest, RoundTripsCanonicalUuid) {
  const auto id = UserId::parse("018f7f54-7c2a-7f31-8f0d-123456789abc");

  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(id->toString(), "018f7f54-7c2a-7f31-8f0d-123456789abc");
}

TEST(UserIdTest, RejectsNonCanonicalUuid) {
  EXPECT_FALSE(UserId::parse("018F7F54-7C2A-7F31-8F0D-123456789ABC"));
  EXPECT_FALSE(UserId::parse("018f7f547c2a7f318f0d123456789abc"));
  EXPECT_FALSE(UserId::parse("018f7f54-7c2a-7f31-8f0d-123456789ab"));
  EXPECT_FALSE(UserId::parse("not-a-uuid"));
}

}  // namespace
}  // namespace rss::domain
