#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "rss/observability/OperationalConfig.h"

namespace {

using namespace std::chrono_literals;

TEST(OperationalConfigTest, ParsesPositiveReportingInterval) {
  EXPECT_EQ(rss::observability::parseReportingIntervalSeconds("30"), 30s);
}

TEST(OperationalConfigTest, AcceptsZeroToDisablePeriodicReporting) {
  EXPECT_EQ(rss::observability::parseReportingIntervalSeconds("0"), 0s);
}

TEST(OperationalConfigTest, AcceptsLargestIntervalRepresentableAsMilliseconds) {
  const auto maximum_seconds = static_cast<std::uint64_t>(
      std::chrono::milliseconds::max().count() / 1000);

  EXPECT_EQ(rss::observability::parseReportingIntervalSeconds(
                std::to_string(maximum_seconds)),
            std::chrono::milliseconds(maximum_seconds * 1000));
}

TEST(OperationalConfigTest, RejectsIntervalThatOverflowsMilliseconds) {
  const auto first_overflowing_second =
      static_cast<std::uint64_t>(std::chrono::milliseconds::max().count() /
                                 1000) +
      1;

  EXPECT_THROW(
      static_cast<void>(rss::observability::parseReportingIntervalSeconds(
          std::to_string(first_overflowing_second))),
      std::invalid_argument);
}

class InvalidReportingIntervalTest
    : public testing::TestWithParam<std::string_view> {};

TEST_P(InvalidReportingIntervalTest, RejectsInvalidValue) {
  EXPECT_THROW(
      static_cast<void>(
          rss::observability::parseReportingIntervalSeconds(GetParam())),
      std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(InvalidValues, InvalidReportingIntervalTest,
                         testing::Values("", "-1", "2s", " 3",
                                         "18446744073709551615"));

}  // namespace
