#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "rss/protocol/TextValidation.h"

namespace {

using rss::protocol::isValidText;
using rss::protocol::trimAsciiWhitespace;

TEST(TextValidationTest, AcceptsAsciiAndRfc3629BoundaryScalars) {
  EXPECT_TRUE(isValidText(""));
  EXPECT_TRUE(isValidText("hello 한글"));
  EXPECT_TRUE(isValidText(std::string("\xC2\x80", 2)));
  EXPECT_TRUE(isValidText(std::string("\xDF\xBF", 2)));
  EXPECT_TRUE(isValidText(std::string("\xE0\xA0\x80", 3)));
  EXPECT_TRUE(isValidText(std::string("\xED\x9F\xBF", 3)));
  EXPECT_TRUE(isValidText(std::string("\xEE\x80\x80", 3)));
  EXPECT_TRUE(isValidText(std::string("\xF0\x90\x80\x80", 4)));
  EXPECT_TRUE(isValidText(std::string("\xF4\x8F\xBF\xBF", 4)));
}

TEST(TextValidationTest, RejectsMalformedUtf8) {
  const std::string invalid[] = {
      std::string("\xC2", 1),
      std::string("\x80", 1),
      std::string("\xE2\x28\xA1", 3),
      std::string("\xC0\xAF", 2),
      std::string("\xE0\x80\x80", 3),
      std::string("\xED\xA0\x80", 3),
      std::string("\xF0\x80\x80\x80", 4),
      std::string("\xF4\x90\x80\x80", 4),
      std::string("\xF5\x80\x80\x80", 4),
  };

  for (const auto& text : invalid) {
    EXPECT_FALSE(isValidText(text));
  }
}

TEST(TextValidationTest, RejectsAsciiControlBytes) {
  for (unsigned int value = 0; value <= 0x1fU; ++value) {
    EXPECT_FALSE(isValidText(std::string(1, static_cast<char>(value))));
  }
  EXPECT_FALSE(isValidText(std::string(1, '\x7f')));
}

TEST(TextValidationTest, TrimsOnlyAsciiWhitespaceAtEdges) {
  EXPECT_EQ(trimAsciiWhitespace(" \t한글\r\n"), "한글");
  EXPECT_EQ(trimAsciiWhitespace("\f\v"), "");

  const std::string non_breaking_space("\xC2\xA0", 2);
  const std::string input = non_breaking_space + "name" + non_breaking_space;
  EXPECT_EQ(trimAsciiWhitespace(input), input);
}

}  // namespace
