#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <sstream>
#include <thread>

#include "rss/observability/PeriodicOverloadReporter.h"

namespace {

using namespace std::chrono_literals;

TEST(PeriodicOverloadReporterTest, EmitsSnapshotsAtConfiguredInterval) {
  std::atomic<int> calls{0};
  std::ostringstream output;
  rss::observability::PeriodicOverloadReporter reporter(
      1ms,
      [&] {
        ++calls;
        rss::net::OverloadSnapshot snapshot;
        snapshot.current_sessions = 7;
        return snapshot;
      },
      output);

  reporter.start();
  const auto deadline = std::chrono::steady_clock::now() + 500ms;
  while (calls.load() == 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  reporter.stop();

  EXPECT_GE(calls.load(), 1);
  EXPECT_NE(output.str().find("\"event\":\"overload_snapshot\""),
            std::string::npos);
  EXPECT_NE(output.str().find("\"phase\":\"periodic\""), std::string::npos);
  EXPECT_NE(output.str().find("\"current_sessions\":7"), std::string::npos);
}

TEST(PeriodicOverloadReporterTest, ZeroIntervalDisablesPeriodicSnapshots) {
  std::atomic<int> calls{0};
  std::ostringstream output;
  rss::observability::PeriodicOverloadReporter reporter(
      0ms,
      [&] {
        ++calls;
        return rss::net::OverloadSnapshot{};
      },
      output);

  reporter.start();
  reporter.stop();

  EXPECT_EQ(calls.load(), 0);
  EXPECT_TRUE(output.str().empty());
}

TEST(PeriodicOverloadReporterTest, StopInterruptsLongReportingInterval) {
  std::ostringstream output;
  rss::observability::PeriodicOverloadReporter reporter(
      std::chrono::hours(1), [] { return rss::net::OverloadSnapshot{}; },
      output);

  reporter.start();
  reporter.stop();

  EXPECT_TRUE(output.str().empty());
}

}  // namespace
