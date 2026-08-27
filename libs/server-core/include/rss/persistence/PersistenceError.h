#pragma once

#include <string>

namespace rss::persistence {

enum class PersistenceErrorKind {
  Busy,
  Unavailable,
  Timeout,
  Constraint,
  InvalidData,
  Stopping,
};

struct PersistenceError {
  PersistenceErrorKind kind{PersistenceErrorKind::Unavailable};
  std::string message;
};

}  // namespace rss::persistence
