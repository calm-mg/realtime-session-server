#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "rss/service/SessionEventHandler.h"

namespace rss::test {

class FakeSessionEventContext final : public service::SessionEventContext {
 private:
  struct State {
    std::mutex mutex;
    std::vector<service::OutboundMessage> messages;
    std::atomic<bool> completed{false};
    bool failed{};
  };

  class Completion final : public service::DeferredSessionCompletion {
   public:
    explicit Completion(std::shared_ptr<State> state)
        : state_(std::move(state)) {}

    bool succeed(std::vector<service::OutboundMessage> messages) override {
      bool expected = false;
      if (!state_->completed.compare_exchange_strong(expected, true)) {
        return false;
      }
      std::lock_guard<std::mutex> lock(state_->mutex);
      for (auto& message : messages) {
        state_->messages.push_back(std::move(message));
      }
      return true;
    }

    bool fail() override {
      bool expected = false;
      if (!state_->completed.compare_exchange_strong(expected, true)) {
        return false;
      }
      std::lock_guard<std::mutex> lock(state_->mutex);
      state_->failed = true;
      return true;
    }

   private:
    std::shared_ptr<State> state_;
  };

 public:
  bool emit(service::OutboundMessage message) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->messages.push_back(std::move(message));
    return true;
  }

  std::shared_ptr<service::DeferredSessionCompletion> defer() override {
    return std::make_shared<Completion>(state_);
  }

  [[nodiscard]] std::vector<service::OutboundMessage> releaseMessages() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return std::move(state_->messages);
  }

  [[nodiscard]] bool failed() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->failed;
  }

 private:
  std::shared_ptr<State> state_{std::make_shared<State>()};
};

}  // namespace rss::test
