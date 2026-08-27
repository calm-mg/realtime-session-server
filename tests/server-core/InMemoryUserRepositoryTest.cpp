#include <gtest/gtest.h>

#include <optional>
#include <utility>
#include <vector>

#include "rss/persistence/InMemoryUserRepository.h"

namespace rss::persistence {
namespace {

TEST(InMemoryUserRepositoryTest, ReusesIdForNormalizedName) {
  InMemoryUserRepository repository;
  std::vector<UserResult> results;

  repository.findOrCreateByNormalizedName(
      {"alice", "Alice"},
      [&](UserResult result) { results.push_back(std::move(result)); });
  repository.findOrCreateByNormalizedName(
      {"alice", "Different Display Name"},
      [&](UserResult result) { results.push_back(std::move(result)); });

  ASSERT_EQ(results.size(), 2U);
  ASSERT_TRUE(results[0].user.has_value());
  ASSERT_TRUE(results[1].user.has_value());
  EXPECT_FALSE(results[0].error.has_value());
  EXPECT_FALSE(results[1].error.has_value());
  EXPECT_EQ(results[0].user->user_id, results[1].user->user_id);
  EXPECT_EQ(results[1].user->display_name, "Alice");
}

TEST(InMemoryUserRepositoryTest, GeneratesDifferentIdsForDifferentNames) {
  InMemoryUserRepository repository;
  std::optional<UserRecord> alice;
  std::optional<UserRecord> bob;

  repository.findOrCreateByNormalizedName(
      {"alice", "Alice"},
      [&](UserResult result) { alice = std::move(result.user); });
  repository.findOrCreateByNormalizedName(
      {"bob", "Bob"}, [&](UserResult result) { bob = std::move(result.user); });

  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(bob.has_value());
  EXPECT_NE(alice->user_id, bob->user_id);
}

TEST(InMemoryUserRepositoryTest, InvokesCallbackOutsideRepositoryLock) {
  InMemoryUserRepository repository;
  std::optional<UserRecord> nested;

  repository.findOrCreateByNormalizedName({"alice", "Alice"}, [&](UserResult) {
    repository.findOrCreateByNormalizedName(
        {"bob", "Bob"},
        [&](UserResult result) { nested = std::move(result.user); });
  });

  ASSERT_TRUE(nested.has_value());
  EXPECT_EQ(nested->normalized_name, "bob");
}

}  // namespace
}  // namespace rss::persistence
