#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>

namespace rss::util {

template <typename T>
class BoundedBlockingQueue {
 public:
  struct OperationResult {
    bool succeeded{};
    std::size_t size{};
  };

  struct TryPopResult {
    std::optional<T> value;
    std::size_t size{};
  };

  explicit BoundedBlockingQueue(std::size_t capacity) : capacity_(capacity) {
    if (capacity == 0) {
      throw std::invalid_argument("queue capacity must be greater than zero");
    }
  }

  OperationResult tryPush(T value) {
    std::size_t new_size = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_ || queue_.size() == capacity_) {
        return {false, queue_.size()};
      }
      queue_.push(std::move(value));
      new_size = queue_.size();
    }
    not_empty_.notify_one();
    return {true, new_size};
  }

  OperationResult push(T value) {
    std::size_t new_size = 0;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      not_full_.wait(lock, [this] {
        return closed_ || queue_.size() < capacity_;
      });
      if (closed_) {
        return {false, queue_.size()};
      }
      queue_.push(std::move(value));
      new_size = queue_.size();
    }
    not_empty_.notify_one();
    return {true, new_size};
  }

  OperationResult pop(T& out) {
    std::size_t new_size = 0;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      not_empty_.wait(lock, [this] { return closed_ || !queue_.empty(); });
      if (queue_.empty()) {
        return {false, 0};
      }
      out = std::move(queue_.front());
      queue_.pop();
      new_size = queue_.size();
    }
    not_full_.notify_one();
    return {true, new_size};
  }

  TryPopResult tryPop() {
    std::optional<T> value;
    std::size_t new_size = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (queue_.empty()) {
        return {std::nullopt, 0};
      }
      value.emplace(std::move(queue_.front()));
      queue_.pop();
      new_size = queue_.size();
    }
    not_full_.notify_one();
    return {std::move(value), new_size};
  }

  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    not_empty_.notify_all();
    not_full_.notify_all();
  }

  [[nodiscard]] std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

  [[nodiscard]] std::size_t capacity() const { return capacity_; }

  [[nodiscard]] bool closed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
  }

 private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  std::queue<T> queue_;
  bool closed_{false};
};

}  // namespace rss::util
