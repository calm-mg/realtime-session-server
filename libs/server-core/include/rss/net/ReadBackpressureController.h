#pragma once

#include <cstddef>

namespace rss::net {

enum class ReadTransition { None, Pause, Resume };

class ReadBackpressureController {
 public:
  ReadBackpressureController(std::size_t high, std::size_t low)
      : high_(high), low_(low) {}

  [[nodiscard]] ReadTransition onInboundSize(std::size_t size) {
    if (!paused_ && size >= high_) {
      paused_ = true;
      return ReadTransition::Pause;
    }
    return ReadTransition::None;
  }

  [[nodiscard]] ReadTransition onCapacityAvailable(std::size_t size) {
    if (paused_ && size <= low_) {
      paused_ = false;
      return ReadTransition::Resume;
    }
    return ReadTransition::None;
  }

  [[nodiscard]] bool paused() const { return paused_; }

 private:
  std::size_t high_;
  std::size_t low_;
  bool paused_{false};
};

}  // namespace rss::net
