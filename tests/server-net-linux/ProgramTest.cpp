#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "Program.h"
#include "ScenarioRunner.h"

namespace {

rss::tools::ScenarioRunResult successfulResult() {
  rss::tools::ScenarioRunResult result;
  result.expected_broadcasts = 1;
  result.received_broadcasts = 1;
  return result;
}

}  // namespace

TEST(ProgramTest, InvalidArgumentsReturnUsageErrorWithoutRunningScenario) {
  const std::array<std::string_view, 2> args{"--unknown", "value"};
  std::ostringstream out;
  std::ostringstream err;
  const auto fail_if_called = [](const rss::tools::ScenarioOptions&,
                                 std::size_t) {
    throw std::runtime_error("scenario must not run");
    return rss::tools::ScenarioRunResult{};
  };

  const auto exit_code = rss::tools::internal::runScenarioProgramWith(
      args, out, err, fail_if_called);

  EXPECT_EQ(exit_code, 2);
  EXPECT_TRUE(out.str().empty());
  EXPECT_NE(err.str().find("Usage: rss_load_scenario_runner"),
            std::string::npos);
}

TEST(ProgramTest, PrintsEnvironmentAndMeasuredRunsButDiscardsOneWarmUp) {
  const std::array<std::string_view, 4> args{"--repeat", "2", "--workers", "3"};
  std::ostringstream out;
  std::ostringstream err;
  std::vector<std::size_t> run_ids;
  const auto run_once = [&](const rss::tools::ScenarioOptions&,
                            std::size_t run_id) {
    run_ids.push_back(run_id);
    auto result = successfulResult();
    if (run_id == 0) {
      result.received_broadcasts = 0;
      result.missing_broadcasts = 1;
    }
    return result;
  };

  const auto exit_code =
      rss::tools::internal::runScenarioProgramWith(args, out, err, run_once);
  const auto output = out.str();

  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err.str().empty());
  EXPECT_EQ(run_ids, (std::vector<std::size_t>{0, 1, 2}));
  EXPECT_EQ(
      static_cast<std::size_t>(std::count(output.begin(), output.end(), '\n')),
      3U);
  EXPECT_TRUE(output.starts_with("environment "));
  EXPECT_NE(output.find("workers=3 "), std::string::npos);
  EXPECT_NE(output.find("\nrun=1 "), std::string::npos);
  EXPECT_NE(output.find("\nrun=2 "), std::string::npos);
  EXPECT_EQ(output.find("\nrun=0 "), std::string::npos);
}

TEST(ProgramTest, SetupFailureResultPrintsRunRowAndReturnsMeasurementFailure) {
  const std::array<std::string_view, 2> args{"--repeat", "2"};
  std::ostringstream out;
  std::ostringstream err;
  std::vector<std::size_t> run_ids;
  const auto run_once = [&](const rss::tools::ScenarioOptions&,
                            std::size_t run_id) {
    run_ids.push_back(run_id);
    auto result = successfulResult();
    if (run_id == 1) {
      result.failed_clients = 1;
    }
    return result;
  };

  const auto exit_code =
      rss::tools::internal::runScenarioProgramWith(args, out, err, run_once);

  EXPECT_EQ(exit_code, 1);
  EXPECT_TRUE(err.str().empty());
  EXPECT_EQ(run_ids, (std::vector<std::size_t>{0, 1, 2}));
  EXPECT_NE(out.str().find("run=1 "), std::string::npos);
  EXPECT_NE(out.str().find("failed_clients=1"), std::string::npos);
  EXPECT_NE(out.str().find("run=2 "), std::string::npos);
}

TEST(ProgramTest, ActualClientSetupFailurePrintsRunRowAndReturnsOne) {
  const std::array<std::string_view, 12> args{
      "--scenario",      "broadcast", "--clients", "2", "--messages", "1",
      "--payload-bytes", "128",       "--repeat",  "1", "--workers",  "1"};
  std::ostringstream out;
  std::ostringstream err;
  const rss::tools::ScenarioRunner runner{{.max_sessions = 1}};
  const auto run_once = [&](const rss::tools::ScenarioOptions& options,
                            std::size_t run_id) {
    return runner.runOnce(options, run_id);
  };

  const auto exit_code =
      rss::tools::internal::runScenarioProgramWith(args, out, err, run_once);

  EXPECT_EQ(exit_code, 1);
  EXPECT_TRUE(err.str().empty());
  EXPECT_NE(out.str().find("\nrun=1 scenario=broadcast clients=2 rooms=1 "),
            std::string::npos);
  EXPECT_NE(out.str().find("failed_clients=2"), std::string::npos);
  EXPECT_NE(out.str().find("rejected_connections=1"), std::string::npos);
}

TEST(ProgramTest, MapsExecutionExceptionToExitCodeThree) {
  const std::array<std::string_view, 2> args{"--repeat", "1"};
  std::ostringstream out;
  std::ostringstream err;
  const auto throw_on_run = [](const rss::tools::ScenarioOptions&,
                               std::size_t) {
    throw std::runtime_error("server start failed");
    return rss::tools::ScenarioRunResult{};
  };

  const auto exit_code = rss::tools::internal::runScenarioProgramWith(
      args, out, err, throw_on_run);

  EXPECT_EQ(exit_code, 3);
  EXPECT_TRUE(out.str().starts_with("environment "));
  EXPECT_NE(err.str().find("server start failed"), std::string::npos);
}

TEST(ProgramTest, PreservesRunnerStandardOutput) {
  const std::array<std::string_view, 2> args{"--repeat", "1"};
  std::ostringstream out;
  std::ostringstream err;
  std::ostringstream console;
  const auto run_once = [](const rss::tools::ScenarioOptions&, std::size_t) {
    std::cout << "server diagnostic\n";
    return successfulResult();
  };

  auto* const previous_buffer = std::cout.rdbuf(console.rdbuf());
  const auto exit_code =
      rss::tools::internal::runScenarioProgramWith(args, out, err, run_once);
  std::cout.rdbuf(previous_buffer);
  const auto output = out.str();

  EXPECT_EQ(exit_code, 0);
  EXPECT_EQ(console.str(), "server diagnostic\nserver diagnostic\n");
  EXPECT_EQ(
      static_cast<std::size_t>(std::count(output.begin(), output.end(), '\n')),
      2U);
}
