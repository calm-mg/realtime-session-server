#include "rss/net/WorkerPool.h"

#include <stdexcept>
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

class QueueOutboundMessageSink final : public service::OutboundMessageSink {
 public:
  QueueOutboundMessageSink(
      util::BoundedBlockingQueue<service::OutboundMessage>& outbox,
      const WorkerPoolConfig& config, std::atomic<bool>& force_stop_requested,
      CompletionNotifier* notifier, OverloadStats* overload_stats)
      : outbox_(outbox),
        config_(config),
        force_stop_requested_(force_stop_requested),
        notifier_(notifier),
        overload_stats_(overload_stats) {}

  bool emit(service::OutboundMessage message) override {
    const auto byte_count = message.bytes.size();
    if (force_stop_requested_.load(std::memory_order_acquire)) {
      return false;
    }
    if (byte_count == 0 ||
        emitted_messages_ >= config_.max_outbound_messages_per_event ||
        byte_count > config_.max_outbound_bytes_per_event - emitted_bytes_) {
      if (overload_stats_ != nullptr) {
        overload_stats_->recordOutboundBudgetRejection();
      }
      return false;
    }

    const auto push_result = outbox_.push(std::move(message));
    if (!push_result.succeeded) {
      return false;
    }

    ++emitted_messages_;
    emitted_bytes_ += byte_count;
    if (overload_stats_ != nullptr) {
      overload_stats_->observeOutboundQueueSize(push_result.size);
    }
    if (notifier_ != nullptr) {
      notifier_->notify();
    }
    return true;
  }

 private:
  util::BoundedBlockingQueue<service::OutboundMessage>& outbox_;
  const WorkerPoolConfig& config_;
  std::atomic<bool>& force_stop_requested_;
  CompletionNotifier* notifier_;
  OverloadStats* overload_stats_;
  std::size_t emitted_messages_{};
  std::size_t emitted_bytes_{};
};

}  // namespace

WorkerPool::WorkerPool(
    util::BoundedBlockingQueue<service::SessionEvent>& inbox,
    util::BoundedBlockingQueue<service::OutboundMessage>& outbox,
    service::SessionEventHandler& handler, WorkerPoolConfig config,
    CompletionNotifier* outbound_notifier,
    CompletionNotifier* input_capacity_notifier, OverloadStats* overload_stats)
    : inbox_(inbox),
      outbox_(outbox),
      handler_(handler),
      config_(config),
      outbound_notifier_(outbound_notifier),
      input_capacity_notifier_(input_capacity_notifier),
      overload_stats_(overload_stats) {
  if (config_.inbound_low_watermark == 0 ||
      config_.max_outbound_messages_per_event == 0 ||
      config_.max_outbound_bytes_per_event == 0) {
    throw std::invalid_argument("worker pool limits must be positive");
  }
}

WorkerPool::~WorkerPool() noexcept {
  forceStop();
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

void WorkerPool::forceStop() {
  force_stop_requested_.store(true, std::memory_order_release);
  inbox_.close();
  outbox_.close();
  sequence_changed_.notify_all();
}

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
    if (pop_result.size == config_.inbound_low_watermark &&
        input_capacity_notifier_ != nullptr) {
      input_capacity_notifier_->notify();
    }

    if (force_stop_requested_.load(std::memory_order_acquire)) {
      return;
    }
    if (!waitForSessionTurn(event)) {
      return;
    }

    try {
      QueueOutboundMessageSink sink(outbox_, config_, force_stop_requested_,
                                    outbound_notifier_, overload_stats_);
      handler_.handle(event, sink);
    } catch (...) {
      completeSessionTurn(event);
      throw;
    }
    completeSessionTurn(event);

    if (force_stop_requested_.load(std::memory_order_acquire)) {
      return;
    }
  }
}

bool WorkerPool::waitForSessionTurn(const service::SessionEvent& event) {
  std::unique_lock<std::mutex> lock(sequence_mutex_);
  ++sequence_by_session_[event.session_id].waiting_workers;
  sequence_changed_.wait(lock, [this, &event] {
    if (force_stop_requested_.load(std::memory_order_acquire)) {
      return true;
    }
    const auto it = sequence_by_session_.find(event.session_id);
    return it != sequence_by_session_.end() && !it->second.active &&
           it->second.next_sequence == event.sequence;
  });
  auto& state = sequence_by_session_.at(event.session_id);
  --state.waiting_workers;
  if (force_stop_requested_.load(std::memory_order_acquire)) {
    if (!state.active && state.waiting_workers == 0) {
      sequence_by_session_.erase(event.session_id);
    }
    return false;
  }
  state.active = true;
  return true;
}

void WorkerPool::completeSessionTurn(const service::SessionEvent& event) {
  {
    std::lock_guard<std::mutex> lock(sequence_mutex_);
    const auto it = sequence_by_session_.find(event.session_id);
    if (it == sequence_by_session_.end()) {
      return;
    }
    it->second.active = false;
    ++it->second.next_sequence;
    if (event.kind == service::SessionEventKind::Disconnected &&
        it->second.waiting_workers == 0) {
      sequence_by_session_.erase(it);
    }
  }
  sequence_changed_.notify_all();
}

}  // namespace rss::net
