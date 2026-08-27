#pragma once

#include <functional>
#include <optional>
#include <string>

#include "rss/persistence/PersistenceError.h"
#include "rss/persistence/UserRecord.h"

namespace rss::persistence {

struct FindOrCreateUser {
  std::string normalized_name;
  std::string display_name;
};

struct UserResult {
  std::optional<UserRecord> user;
  std::optional<PersistenceError> error;
};

using UserCallback = std::function<void(UserResult)>;

class UserRepository {
 public:
  virtual ~UserRepository() = default;

  virtual void findOrCreateByNormalizedName(FindOrCreateUser request,
                                            UserCallback callback) = 0;
};

}  // namespace rss::persistence
