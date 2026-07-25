#pragma once

#include <cstddef>

namespace rss::net::detail {

class AcceptBatchLimiter {
 public:
  explicit AcceptBatchLimiter(std::size_t limit) : remaining_(limit) {}

  [[nodiscard]] bool tryAcquire(bool stop_requested) noexcept {
    if (stop_requested || remaining_ == 0) {
      return false;
    }
    --remaining_;
    return true;
  }

 private:
  std::size_t remaining_;
};

}  // namespace rss::net::detail
