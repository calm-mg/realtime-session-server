#include <gtest/gtest.h>

#include <array>
#include <stdexcept>
#include <string_view>

#include "rss/tools/ScenarioOptions.h"

namespace {

TEST(ScenarioOptionsTest, ParsesBroadcastArguments) {
  const std::array<std::string_view, 12> args{
      "--scenario",      "broadcast", "--clients", "20", "--messages", "50",
      "--payload-bytes", "512",       "--repeat",  "3",  "--workers",  "2"};

  const auto options = rss::tools::parseScenarioOptions(args);

  EXPECT_EQ(options.scenario, rss::tools::ScenarioKind::Broadcast);
  EXPECT_EQ(options.clients, 20U);
  EXPECT_EQ(options.messages_per_sender, 50U);
  EXPECT_EQ(options.payload_bytes, 512U);
  EXPECT_EQ(options.repeats, 3U);
  EXPECT_EQ(options.worker_count, 2U);
}

TEST(ScenarioOptionsTest, RejectsRoomCountAboveClientCount) {
  const std::array<std::string_view, 6> args{
      "--scenario", "multi-room", "--clients", "2", "--rooms", "3"};

  EXPECT_THROW(rss::tools::parseScenarioOptions(args), std::invalid_argument);
}

TEST(ScenarioOptionsTest, RejectsSlowClientCountAtClientCount) {
  const std::array<std::string_view, 6> args{
      "--scenario", "slow-client", "--clients", "2", "--slow-clients", "2"};

  EXPECT_THROW(rss::tools::parseScenarioOptions(args), std::invalid_argument);
}

}  // namespace
