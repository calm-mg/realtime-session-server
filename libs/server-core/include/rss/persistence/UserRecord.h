#pragma once

#include <string>

#include "rss/domain/UserId.h"

namespace rss::persistence {

struct UserRecord {
  domain::UserId user_id;
  std::string normalized_name;
  std::string display_name;
};

}  // namespace rss::persistence
