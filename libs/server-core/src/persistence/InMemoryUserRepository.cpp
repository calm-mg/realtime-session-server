#include "rss/persistence/InMemoryUserRepository.h"

#include <utility>

namespace rss::persistence {

void InMemoryUserRepository::findOrCreateByNormalizedName(
    FindOrCreateUser request, UserCallback callback) {
  UserRecord user;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto existing =
        users_by_normalized_name_.find(request.normalized_name);
    if (existing != users_by_normalized_name_.end()) {
      user = existing->second;
    } else {
      user = UserRecord{nextUserId(), std::move(request.normalized_name),
                        std::move(request.display_name)};
      users_by_normalized_name_.emplace(user.normalized_name, user);
    }
  }

  callback(UserResult{std::move(user), std::nullopt});
}

domain::UserId InMemoryUserRepository::nextUserId() {
  domain::UserId::Bytes bytes{};
  auto value = next_id_++;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[bytes.size() - 1 - index] = static_cast<std::uint8_t>(value & 0xffU);
    value >>= 8U;
  }
  return domain::UserId{bytes};
}

}  // namespace rss::persistence
