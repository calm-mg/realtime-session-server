#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "rss/protocol/ProtocolError.h"
#include "rss/protocol/StructuredPayload.h"

namespace {

using rss::protocol::decodeStructuredValue;
using rss::protocol::encodeStructuredValue;
using rss::protocol::ProtocolError;
using rss::protocol::StructuredPayload;
using rss::protocol::StructuredPayloadBuilder;

TEST(StructuredPayloadTest, EncodesReservedBytesAndRoundTripsUtf8) {
  const std::string original = "한글%|=";
  EXPECT_EQ(encodeStructuredValue(original), "한글%25%7C%3D");
  EXPECT_EQ(decodeStructuredValue("한글%25%7c%3d"), original);
}

TEST(StructuredPayloadTest, BuildsAndParsesStatusAndFields) {
  const auto wire = StructuredPayloadBuilder("OK")
                        .addField("event", "CHAT")
                        .addField("name", "kim|role=admin%")
                        .addField("message", "a=b|c")
                        .build();

  EXPECT_EQ(wire, "OK|event=CHAT|name=kim%7Crole%3Dadmin%25|message=a%3Db%7Cc");
  const auto parsed = StructuredPayload::parse(wire);
  ASSERT_TRUE(parsed.status().has_value());
  EXPECT_EQ(*parsed.status(), "OK");
  EXPECT_EQ(parsed.requireField("name"), "kim|role=admin%");
  EXPECT_EQ(parsed.requireField("message"), "a=b|c");
  ASSERT_EQ(parsed.fields().size(), 3U);
  EXPECT_EQ(parsed.fields()[0].first, "event");
  EXPECT_EQ(parsed.fields()[1].first, "name");
  EXPECT_EQ(parsed.fields()[2].first, "message");
}

TEST(StructuredPayloadTest, ParsesPayloadWithoutStatusAndEmptyValue) {
  const auto parsed = StructuredPayload::parse("event=CHAT|message=");

  EXPECT_FALSE(parsed.status().has_value());
  EXPECT_EQ(parsed.requireField("event"), "CHAT");
  EXPECT_EQ(parsed.requireField("message"), "");
  EXPECT_FALSE(parsed.field("name").has_value());
}

TEST(StructuredPayloadTest, RejectsMalformedGrammarAndEscapes) {
  const std::string_view malformed[] = {
      "",
      "event",
      "=value",
      "event=a=b",
      "event=ONE|event=TWO",
      "event=CHAT|",
      "event=%",
      "event=%2",
      "event=%GG",
      "NO|event=CHAT",
  };

  for (const auto payload : malformed) {
    EXPECT_THROW(StructuredPayload::parse(payload), ProtocolError)
        << std::string(payload);
  }
}

TEST(StructuredPayloadTest, RejectsInvalidDecodedTextAndMissingField) {
  EXPECT_THROW(StructuredPayload::parse("name=%C0%AF"), ProtocolError);
  EXPECT_THROW(StructuredPayload::parse("name=line%0Afeed"), ProtocolError);

  const auto parsed = StructuredPayload::parse("event=JOIN");
  EXPECT_THROW(static_cast<void>(parsed.requireField("room_id")),
               ProtocolError);
}

TEST(StructuredPayloadTest, BuilderRejectsInvalidStatusKeysAndValues) {
  EXPECT_THROW(StructuredPayloadBuilder("ERROR"), ProtocolError);

  StructuredPayloadBuilder builder;
  EXPECT_THROW(builder.addField("", "value"), ProtocolError);
  EXPECT_THROW(builder.addField("bad-key", "value"), ProtocolError);
  EXPECT_THROW(builder.addField("name", std::string("\xC0\xAF", 2)),
               ProtocolError);

  builder.addField("event", "JOIN");
  EXPECT_THROW(builder.addField("event", "LEAVE"), ProtocolError);
}

TEST(StructuredPayloadTest, BuilderRequiresAtLeastOneField) {
  EXPECT_THROW(static_cast<void>(StructuredPayloadBuilder().build()),
               ProtocolError);
  EXPECT_THROW(static_cast<void>(StructuredPayloadBuilder("OK").build()),
               ProtocolError);
}

}  // namespace
