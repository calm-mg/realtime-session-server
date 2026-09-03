#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
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
