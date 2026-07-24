#include <gtest/gtest.h>

#include <chrono>
#include <vector>

#include "rss/tools/LatencyStats.h"

namespace {

using Sample = std::chrono::microseconds;
using rss::tools::latencyReport;

TEST(LatencyStatsTest, CalculatesNearestRankPercentiles) {
  const std::vector<Sample> samples{
      Sample{3000}, Sample{1000}, Sample{5000}, Sample{2000}, Sample{4000},
  };

  const auto report = latencyReport(samples);

  EXPECT_EQ(report.sample_count, 5);
  EXPECT_EQ(report.min_us, 1000);
  EXPECT_EQ(report.p50_us, 3000);
  EXPECT_EQ(report.p95_us, 5000);
  EXPECT_EQ(report.p99_us, 5000);
  EXPECT_EQ(report.max_us, 5000);
}

TEST(LatencyStatsTest, ReturnsZeroValuesForEmptySamples) {
  const auto report = latencyReport({});

  EXPECT_EQ(report.sample_count, 0);
  EXPECT_EQ(report.min_us, 0);
  EXPECT_EQ(report.p50_us, 0);
  EXPECT_EQ(report.p95_us, 0);
  EXPECT_EQ(report.p99_us, 0);
  EXPECT_EQ(report.max_us, 0);
}

}  // namespace
