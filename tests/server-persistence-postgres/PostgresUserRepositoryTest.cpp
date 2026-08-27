#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "rss/persistence/postgres/PostgresUserRepository.h"

namespace rss::persistence::postgres {
namespace {

using namespace std::chrono_literals;

std::string testDatabaseUrl() {
  const auto* value = std::getenv("RSS_TEST_DATABASE_URL");
  return value == nullptr ? std::string{} : std::string{value};
}

std::string uniqueName(std::string_view prefix) {
  static std::atomic<std::uint64_t> sequence{0};
  return std::string(prefix) + "_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()) +
         "_" + std::to_string(sequence.fetch_add(1));
}

class PostgresUserRepositoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    database_url_ = testDatabaseUrl();
    if (database_url_.empty()) {
      GTEST_SKIP() << "RSS_TEST_DATABASE_URL is not configured";
    }
  }

  std::string database_url_;
};

TEST_F(PostgresUserRepositoryTest, ConcurrentFindOrCreateReturnsOneUserId) {
  constexpr std::size_t kRequests = 16;
  PostgresExecutor executor(database_url_, 4, 32);
  executor.start();
  PostgresUserRepository repository(executor);
  const auto name = uniqueName("concurrent");
  std::mutex mutex;
  std::condition_variable changed;
  std::vector<UserResult> results;

  for (std::size_t index = 0; index < kRequests; ++index) {
    repository.findOrCreateByNormalizedName(
        {name, name}, [&](UserResult result) {
          {
            std::lock_guard<std::mutex> lock(mutex);
            results.push_back(std::move(result));
          }
          changed.notify_all();
        });
  }

  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(changed.wait_for(lock, 10s,
                                 [&] { return results.size() == kRequests; }));
  }
  executor.stop();

  ASSERT_EQ(results.size(), kRequests);
  ASSERT_TRUE(results.front().user.has_value());
  const auto expected_id = results.front().user->user_id;
  for (const auto& result : results) {
    ASSERT_TRUE(result.user.has_value());
    EXPECT_FALSE(result.error.has_value());
    EXPECT_EQ(result.user->user_id, expected_id);
  }
}

TEST_F(PostgresUserRepositoryTest, ReconnectFromNewRepositoryReusesUserId) {
  const auto name = uniqueName("reconnect");
  std::optional<domain::UserId> first_id;
  std::optional<UserResult> first_result;
  {
    PostgresExecutor executor(database_url_, 1, 4);
    executor.start();
    PostgresUserRepository repository(executor);
    repository.findOrCreateByNormalizedName(
        {name, name},
        [&](UserResult result) { first_result = std::move(result); });
    executor.stop();
  }
  ASSERT_TRUE(first_result.has_value());
  ASSERT_TRUE(first_result->user.has_value());
  first_id = first_result->user->user_id;

  std::optional<domain::UserId> second_id;
  std::optional<UserResult> second_result;
  {
    PostgresExecutor executor(database_url_, 1, 4);
    executor.start();
    PostgresUserRepository repository(executor);
    repository.findOrCreateByNormalizedName(
        {name, name},
        [&](UserResult result) { second_result = std::move(result); });
    executor.stop();
  }
  ASSERT_TRUE(second_result.has_value());
  ASSERT_TRUE(second_result->user.has_value());
  second_id = second_result->user->user_id;

  ASSERT_TRUE(first_id.has_value());
  ASSERT_TRUE(second_id.has_value());
  EXPECT_EQ(first_id, second_id);
}

TEST_F(PostgresUserRepositoryTest, ReportsBusyWhenQueueIsFull) {
  PostgresExecutor executor(database_url_, 1, 1);
  executor.start();
  PostgresUserRepository repository(executor);
  std::mutex mutex;
  std::condition_variable changed;
  bool blocker_entered = false;
  bool release_blocker = false;
  ASSERT_TRUE(executor.submit([&](pg_conn*) {
    std::unique_lock<std::mutex> lock(mutex);
    blocker_entered = true;
    changed.notify_all();
    changed.wait(lock, [&] { return release_blocker; });
  }));
  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(changed.wait_for(lock, 5s, [&] { return blocker_entered; }));
  }
  ASSERT_TRUE(executor.submit([](pg_conn*) {}));

  std::optional<UserResult> result;
  repository.findOrCreateByNormalizedName(
      {uniqueName("busy"), "busy"},
      [&](UserResult value) { result = std::move(value); });

  {
    std::lock_guard<std::mutex> lock(mutex);
    release_blocker = true;
  }
  changed.notify_all();
  executor.stop();

  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result->error.has_value());
  EXPECT_EQ(result->error->kind, PersistenceErrorKind::Busy);
}

TEST_F(PostgresUserRepositoryTest, ReportsStoppingExactlyOnceAfterStop) {
  PostgresExecutor executor(database_url_, 1, 1);
  executor.start();
  executor.stop();
  PostgresUserRepository repository(executor);
  int callbacks = 0;
  std::optional<UserResult> result;

  repository.findOrCreateByNormalizedName({uniqueName("stopping"), "stopping"},
                                          [&](UserResult value) {
                                            ++callbacks;
                                            result = std::move(value);
                                          });

  EXPECT_EQ(callbacks, 1);
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result->error.has_value());
  EXPECT_EQ(result->error->kind, PersistenceErrorKind::Stopping);
}

TEST(PostgresExecutorTest, RejectsInvalidConnectionAtStartup) {
  PostgresExecutor executor(
      "host=127.0.0.1 port=1 dbname=missing connect_timeout=1", 1, 1);
  EXPECT_THROW(executor.start(), std::runtime_error);
  EXPECT_FALSE(executor.isRunning());
}

TEST(PostgresUserRepositoryUnitTest, ReportsStoppingExactlyOnceBeforeStart) {
  PostgresExecutor executor("unused", 1, 1);
  PostgresUserRepository repository(executor);
  int callbacks = 0;
  std::optional<UserResult> result;

  repository.findOrCreateByNormalizedName({"alice", "Alice"},
                                          [&](UserResult value) {
                                            ++callbacks;
                                            result = std::move(value);
                                          });

  EXPECT_EQ(callbacks, 1);
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result->error.has_value());
  EXPECT_EQ(result->error->kind, PersistenceErrorKind::Stopping);
}

}  // namespace
}  // namespace rss::persistence::postgres
