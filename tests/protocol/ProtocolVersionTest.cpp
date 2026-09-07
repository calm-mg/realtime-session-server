#include <gtest/gtest.h>

#include "rss/protocol/ProtocolError.h"
#include "rss/protocol/ProtocolVersion.h"

using rss::protocol::decodeVersionRequest;
using rss::protocol::decodeVersionResponse;
using rss::protocol::encodeVersionRequest;
using rss::protocol::encodeVersionResponse;
using rss::protocol::negotiateVersion;
using rss::protocol::ProtocolError;

TEST(ProtocolVersionTest, RoundTripsBoundariesAndSelectsIntersection) {
  const auto range = decodeVersionRequest(encodeVersionRequest({1, 65535}));
  EXPECT_EQ(range.min_version, 1);
  EXPECT_EQ(range.max_version, 65535);
  EXPECT_EQ(decodeVersionResponse(encodeVersionResponse(65535)), 65535);
  EXPECT_EQ(negotiateVersion(range), 1);
  EXPECT_FALSE(negotiateVersion({2, 65535}).has_value());
  EXPECT_THROW(encodeVersionRequest({0, 1}), ProtocolError);
  EXPECT_THROW(encodeVersionRequest({2, 1}), ProtocolError);
  EXPECT_THROW(encodeVersionResponse(0), ProtocolError);
}

TEST(ProtocolVersionTest, RejectsMalformedRequests) {
  for (const auto* payload :
       {"", "min_version=1", "min_version=1|max_version=1|extra=1",
        "OK|min_version=1|max_version=1", "min_version=1|min_version=1",
        "min_version=0|max_version=1", "min_version=2|max_version=1",
        "min_version=-1|max_version=1", "min_version=+1|max_version=1",
        "min_version=1|max_version=65536", "min_version=1|max_version=1x",
        "min_version= 1|max_version=1", "min_version=1|max_version="}) {
    EXPECT_THROW(decodeVersionRequest(payload), ProtocolError) << payload;
  }
}

TEST(ProtocolVersionTest, RejectsMalformedResponses) {
  for (const auto* payload :
       {"", "version=1", "FAIL|version=1", "OK|version=0", "OK|version=65536",
        "OK|version=1|extra=1", "OK|version=1|version=1", "OK|version=+1",
        "OK|version=1 ", "OK|version="}) {
    EXPECT_THROW(decodeVersionResponse(payload), ProtocolError) << payload;
  }
}
