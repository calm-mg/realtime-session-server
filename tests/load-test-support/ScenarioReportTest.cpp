#include <gtest/gtest.h>

#include <array>

#include "rss/tools/ScenarioReport.h"

namespace {

TEST(ScenarioReportTest, CalculatesExpectedBroadcastsAcrossRooms) {
  const std::array<std::size_t, 2> room_sizes{3, 2};

  EXPECT_EQ(rss::tools::expectedBroadcasts(room_sizes, 4), 52U);
}

TEST(ScenarioReportTest, FailsWhenAnyObservableErrorExists) {
  rss::tools::ScenarioRunResult result;
  result.expected_broadcasts = 10;
  result.received_broadcasts = 9;
  result.missing_broadcasts = 1;

  EXPECT_FALSE(
      rss::tools::isSuccessful(rss::tools::ScenarioKind::Broadcast, result, 0));
}

TEST(ScenarioReportTest, RequiresConfiguredSlowClientDisconnects) {
  rss::tools::ScenarioRunResult result;
  result.overload.slow_client_disconnects = 1;

  EXPECT_FALSE(rss::tools::isSuccessful(rss::tools::ScenarioKind::SlowClient,
                                        result, 2));

  result.overload.slow_client_disconnects = 2;

  EXPECT_TRUE(rss::tools::isSuccessful(rss::tools::ScenarioKind::SlowClient,
                                       result, 2));
}

}  // namespace
