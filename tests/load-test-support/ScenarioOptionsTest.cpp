#include <gtest/gtest.h>

#include <array>
#include <limits>
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

TEST(ScenarioOptionsTest, EnforcesChatPayloadLimit) {
  const std::array<std::string_view, 2> maximum{"--payload-bytes", "1291"};
  EXPECT_EQ(rss::tools::parseScenarioOptions(maximum).payload_bytes, 1291U);

  const std::array<std::string_view, 2> oversized{"--payload-bytes", "1292"};
  EXPECT_THROW(rss::tools::parseScenarioOptions(oversized),
               std::invalid_argument);
}

TEST(ScenarioOptionsTest,
     ParsesSustainedLoadControlsAndAllowsDisabledControls) {
  const std::array<std::string_view, 6> args{"--rate-per-client", "25",
                                             "--max-in-flight",   "8",
                                             "--timeout-seconds", "90"};
  const auto options = rss::tools::parseScenarioOptions(args);
  EXPECT_EQ(options.rate_per_client, 25U);
  EXPECT_EQ(options.max_in_flight, 8U);
  EXPECT_EQ(options.timeout_seconds, 90U);
  const std::array<std::string_view, 4> disabled{"--rate-per-client", "0",
                                                 "--max-in-flight", "0"};
  const auto burst = rss::tools::parseScenarioOptions(disabled);
  EXPECT_EQ(burst.rate_per_client, 0U);
  EXPECT_EQ(burst.max_in_flight, 0U);
}

TEST(ScenarioOptionsTest, RejectsExcessiveReceiptStorageBeforeRunning) {
  const std::array<std::string_view, 4> args{"--clients", "100", "--messages",
                                             "501"};
  EXPECT_THROW(rss::tools::parseScenarioOptions(args), std::invalid_argument);
  const std::array<std::string_view, 4> boundary{"--clients", "100",
                                                 "--messages", "500"};
  EXPECT_NO_THROW(rss::tools::parseScenarioOptions(boundary));
}

TEST(ScenarioOptionsTest, CountsUnevenRoomsAndExcludesSlowReadersFromBudget) {
  const std::array<std::string_view, 8> rooms{
      "--scenario", "multi-room", "--clients",  "3",
      "--rooms",    "2",          "--messages", "1000000"};
  EXPECT_NO_THROW(rss::tools::parseScenarioOptions(rooms));
  const std::array<std::string_view, 8> oversized{
      "--scenario", "multi-room", "--clients",  "3",
      "--rooms",    "2",          "--messages", "1000001"};
  EXPECT_THROW(rss::tools::parseScenarioOptions(oversized),
               std::invalid_argument);
  const std::array<std::string_view, 8> slow{
      "--scenario",     "slow-client", "--clients",  "3",
      "--slow-clients", "2",           "--messages", "5000000"};
  EXPECT_NO_THROW(rss::tools::parseScenarioOptions(slow));
}

TEST(ScenarioOptionsTest, RejectsExcessiveThreadsAndControlBounds) {
  for (const auto option : {"--clients", "--workers"}) {
    const std::array<std::string_view, 2> args{option, "1001"};
    EXPECT_THROW(rss::tools::parseScenarioOptions(args), std::invalid_argument);
  }
  for (const auto value : {"0", "3601"}) {
    const std::array<std::string_view, 2> args{"--timeout-seconds", value};
    EXPECT_THROW(rss::tools::parseScenarioOptions(args), std::invalid_argument);
  }
  const std::array<std::string_view, 2> rate{"--rate-per-client", "1000001"};
  EXPECT_THROW(rss::tools::parseScenarioOptions(rate), std::invalid_argument);
}

TEST(ScenarioOptionsTest,
     DirectValidationRejectsOverflowWithoutMultiplication) {
  rss::tools::ScenarioOptions options;
  options.messages_per_sender = std::numeric_limits<std::size_t>::max();
  EXPECT_THROW(rss::tools::validateScenarioOptions(options),
               std::invalid_argument);
  options.messages_per_sender = 1;
  options.scenario = rss::tools::ScenarioKind::MultiRoom;
  options.rooms = 0;
  EXPECT_THROW(rss::tools::validateScenarioOptions(options),
               std::invalid_argument);
}

}  // namespace
