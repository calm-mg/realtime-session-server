#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "rss/persistence/UserRepository.h"

namespace rss::persistence {

class InMemoryUserRepository final : public UserRepository {
 public:
  void findOrCreateByNormalizedName(FindOrCreateUser request,
                                    UserCallback callback) override;

 private:
  [[nodiscard]] domain::UserId nextUserId();

  std::mutex mutex_;
  std::unordered_map<std::string, UserRecord> users_by_normalized_name_;
  std::uint64_t next_id_{1};
};

}  // namespace rss::persistence
