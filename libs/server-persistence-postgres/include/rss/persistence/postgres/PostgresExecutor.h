#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rss/util/BoundedBlockingQueue.h"

struct pg_conn;

namespace rss::persistence::postgres {

class PostgresExecutor {
 public:
  using Task = std::function<void(pg_conn*)>;

  PostgresExecutor(std::string connection_string, std::size_t worker_count,
                   std::size_t queue_capacity);
  ~PostgresExecutor() noexcept;

  PostgresExecutor(const PostgresExecutor&) = delete;
  PostgresExecutor& operator=(const PostgresExecutor&) = delete;

  void start();
  [[nodiscard]] bool submit(Task task);
  void stop() noexcept;
  [[nodiscard]] bool isRunning() const noexcept;

 private:
  void run(pg_conn* connection) noexcept;

  std::string connection_string_;
  std::size_t worker_count_{};
  util::BoundedBlockingQueue<Task> tasks_;
  mutable std::mutex lifecycle_mutex_;
  std::vector<pg_conn*> connections_;
  std::vector<std::thread> threads_;
  std::atomic<bool> accepting_{false};
  bool started_{};
};

}  // namespace rss::persistence::postgres
