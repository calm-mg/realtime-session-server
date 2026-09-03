#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <ostream>
#include <thread>

#include "rss/net/OverloadStats.h"

namespace rss::observability {

class PeriodicOverloadReporter {
 public:
  using SnapshotProvider = std::function<net::OverloadSnapshot()>;
  using Task = std::function<void()>;
  using ThreadFactory = std::function<std::thread(Task)>;

  PeriodicOverloadReporter(std::chrono::milliseconds interval,
                           SnapshotProvider snapshot_provider,
                           std::ostream& output);
  PeriodicOverloadReporter(std::chrono::milliseconds interval,
                           SnapshotProvider snapshot_provider,
                           std::ostream& output, ThreadFactory thread_factory);
  ~PeriodicOverloadReporter();

  PeriodicOverloadReporter(const PeriodicOverloadReporter&) = delete;
  PeriodicOverloadReporter& operator=(const PeriodicOverloadReporter&) = delete;

  [[nodiscard]] bool start() noexcept;
  void stop() noexcept;

 private:
  void run() noexcept;

  std::chrono::milliseconds interval_;
  SnapshotProvider snapshot_provider_;
  ThreadFactory thread_factory_;
  std::ostream& output_;
  std::mutex mutex_;
  std::condition_variable stopped_;
  std::thread thread_;
  bool stop_requested_{false};
};

}  // namespace rss::observability
