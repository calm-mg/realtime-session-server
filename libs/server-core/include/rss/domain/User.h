#pragma once

#include <string>

#include "rss/domain/UserId.h"

namespace rss::domain {

struct User {
  UserId id;
  std::uint64_t session_id{};
  std::string name;
};

}  // namespace rss::domain
