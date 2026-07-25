#include "rss/net/WorkerPool.h"

#include <utility>

namespace rss::net {
namespace {

class ActiveWorkerGuard {
 public:
  explicit ActiveWorkerGuard(std::atomic<std::size_t>& active_workers)
      : active_workers_(active_workers) {}

  ~ActiveWorkerGuard() {
    active_workers_.fetch_sub(1, std::memory_order_release);
  }

 private:
  std::atomic<std::size_t>& active_workers_;
};

}  // namespace

WorkerPool::WorkerPool(
    util::BoundedBlockingQueue<service::SessionEvent>& inbox,
    util::BoundedBlockingQueue<service::OutboundMessage>& outbox,
    service::SessionEventHandler& handler, std::size_t inbound_low_watermark,
    CompletionNotifier* outbound_notifier,
    CompletionNotifier* input_capacity_notifier)
    : inbox_(inbox),
      outbox_(outbox),
      handler_(handler),
      inbound_low_watermark_(inbound_low_watermark),
      outbound_notifier_(outbound_notifier),
      input_capacity_notifier_(input_capacity_notifier) {}

WorkerPool::~WorkerPool() noexcept {
  inbox_.close();
  outbox_.close();
  join();
}

void WorkerPool::start(std::size_t thread_count) {
  if (!threads_.empty()) {
    return;
  }
  if (thread_count == 0) {
    thread_count = 1;
  }
  threads_.reserve(thread_count);
  for (std::size_t i = 0; i < thread_count; ++i) {
    active_workers_.fetch_add(1, std::memory_order_release);
    try {
      threads_.emplace_back([this] {
        ActiveWorkerGuard guard(active_workers_);
        run();
      });
    } catch (...) {
      active_workers_.fetch_sub(1, std::memory_order_release);
      throw;
    }
  }
}

void WorkerPool::beginStop() { inbox_.close(); }

bool WorkerPool::finished() const {
  return active_workers_.load(std::memory_order_acquire) == 0;
}

void WorkerPool::join() {
  for (auto& thread : threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  threads_.clear();
}

void WorkerPool::run() {
  service::SessionEvent event;
  while (true) {
    const auto pop_result = inbox_.pop(event);
    if (!pop_result.succeeded) {
      return;
    }
    if (pop_result.size == inbound_low_watermark_ &&
        input_capacity_notifier_ != nullptr) {
      input_capacity_notifier_->notify();
    }

    auto messages = handler_.handle(event);
    for (auto& message : messages) {
      const auto push_result = outbox_.push(std::move(message));
      if (!push_result.succeeded) {
        return;
      }
      if (outbound_notifier_ != nullptr) {
        outbound_notifier_->notify();
      }
    }
  }
}

}  // namespace rss::net
