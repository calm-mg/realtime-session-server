#include "rss/observability/PeriodicOverloadReporter.h"

#include <stdexcept>
#include <utility>

#include "rss/observability/OperationalLogFormatter.h"

namespace rss::observability {

PeriodicOverloadReporter::PeriodicOverloadReporter(
    std::chrono::milliseconds interval, SnapshotProvider snapshot_provider,
    std::ostream& output)
    : interval_(interval),
      snapshot_provider_(std::move(snapshot_provider)),
      output_(output) {
  if (interval_ < std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("reporting interval must not be negative");
  }
  if (!snapshot_provider_) {
    throw std::invalid_argument("snapshot provider is required");
  }
}

PeriodicOverloadReporter::~PeriodicOverloadReporter() { stop(); }

void PeriodicOverloadReporter::start() {
  if (interval_ == std::chrono::milliseconds::zero()) {
    return;
  }
  if (thread_.joinable()) {
    throw std::logic_error("overload reporter is already running");
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_requested_ = false;
  }
  thread_ = std::thread(&PeriodicOverloadReporter::run, this);
}

void PeriodicOverloadReporter::stop() noexcept {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_requested_ = true;
  }
  stopped_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void PeriodicOverloadReporter::run() noexcept {
  std::unique_lock<std::mutex> lock(mutex_);
  while (
      !stopped_.wait_for(lock, interval_, [this] { return stop_requested_; })) {
    lock.unlock();
    try {
      output_ << formatOverloadSnapshot(currentUnixTimeMilliseconds(),
                                        SnapshotPhase::Periodic,
                                        snapshot_provider_());
      output_.flush();
    } catch (...) {
      // Observability must not terminate the server when formatting, snapshot
      // collection, or output fails.
    }
    lock.lock();
  }
}

}  // namespace rss::observability
