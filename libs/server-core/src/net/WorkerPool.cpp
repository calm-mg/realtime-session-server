#include "rss/net/WorkerPool.h"

#include <memory>
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

struct DeferredCompletionState {
  std::mutex mutex;
  bool completed{};
  bool handler_returned{};
  std::shared_ptr<service::DeferredCompletionPayload> inline_payload;
};

class QueueDeferredSessionCompletion final
    : public service::DeferredSessionCompletion {
 public:
  QueueDeferredSessionCompletion(
      util::BoundedBlockingQueue<service::SessionEvent>& inbox,
      std::shared_ptr<std::atomic<bool>> force_stop_requested,
      std::uint64_t session_id, std::uint64_t sequence,
      std::shared_ptr<DeferredCompletionState> state)
      : inbox_(inbox),
        force_stop_requested_(std::move(force_stop_requested)),
        session_id_(session_id),
        sequence_(sequence),
        state_(std::move(state)) {}

  bool succeed(std::vector<service::OutboundMessage> messages) override {
    return complete(false, std::move(messages));
  }

  bool fail() override { return complete(true, {}); }

 private:
  bool complete(bool failed, std::vector<service::OutboundMessage> messages) {
    if (force_stop_requested_->load(std::memory_order_acquire)) {
      return false;
    }
    auto payload = std::make_shared<service::DeferredCompletionPayload>();
    payload->failed = failed;
    payload->messages = std::move(messages);
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->completed) {
        return false;
      }
      state_->completed = true;
      if (!state_->handler_returned) {
        state_->inline_payload = std::move(payload);
        return true;
      }
    }
    return inbox_
        .push(
            service::SessionEvent{service::SessionEventKind::DeferredCompletion,
                                  session_id_,
                                  {},
                                  sequence_,
                                  std::move(payload)})
        .succeeded;
  }

  util::BoundedBlockingQueue<service::SessionEvent>& inbox_;
  std::shared_ptr<std::atomic<bool>> force_stop_requested_;
  std::uint64_t session_id_{};
  std::uint64_t sequence_{};
  std::shared_ptr<DeferredCompletionState> state_;
};

class WorkerSessionEventContext final : public service::SessionEventContext {
 public:
  WorkerSessionEventContext(
      const WorkerPoolConfig& config,
      const std::shared_ptr<std::atomic<bool>>& force_stop_requested,
      OverloadStats* overload_stats,
      util::BoundedBlockingQueue<service::SessionEvent>& inbox,
      const service::SessionEvent& event)
      : config_(config),
        force_stop_requested_(force_stop_requested),
        overload_stats_(overload_stats),
        inbox_(inbox),
        event_(event) {}

  bool emit(service::OutboundMessage message) override {
    const auto byte_count = message.bytes.size();
    if (force_stop_requested_->load(std::memory_order_acquire)) {
      return false;
    }
    if (disconnect_requested_) {
      return false;
    }
    if (message.kind == service::OutboundMessageKind::DisconnectSession) {
      if (message.session_id != event_.session_id || byte_count != 0) {
        return false;
      }
      messages_.push_back(std::move(message));
      disconnect_requested_ = true;
      return true;
    }
    if (byte_count == 0 ||
        emitted_messages_ >= config_.max_outbound_messages_per_event ||
        byte_count > config_.max_outbound_bytes_per_event - emitted_bytes_) {
      if (overload_stats_ != nullptr) {
        overload_stats_->recordOutboundBudgetRejection();
      }
      return false;
    }

    messages_.push_back(std::move(message));
    ++emitted_messages_;
    emitted_bytes_ += byte_count;
    return true;
  }

  std::shared_ptr<service::DeferredSessionCompletion> defer() override {
    if (deferred_ || !messages_.empty()) {
      throw std::logic_error(
          "session event can only defer once before emitting output");
    }
    deferred_ = true;
    deferred_state_ = std::make_shared<DeferredCompletionState>();
    return std::make_shared<QueueDeferredSessionCompletion>(
        inbox_, force_stop_requested_, event_.session_id, event_.sequence,
        deferred_state_);
  }

  std::vector<service::OutboundMessage> release() {
    return std::move(messages_);
  }

  [[nodiscard]] bool deferred() const noexcept { return deferred_; }

  std::optional<service::SessionEvent> finishDeferredHandler() {
    if (!deferred_) {
      return std::nullopt;
    }
    std::shared_ptr<service::DeferredCompletionPayload> payload;
    {
      std::lock_guard<std::mutex> lock(deferred_state_->mutex);
      deferred_state_->handler_returned = true;
      payload = std::move(deferred_state_->inline_payload);
    }
    if (payload == nullptr) {
      return std::nullopt;
    }
    return service::SessionEvent{service::SessionEventKind::DeferredCompletion,
                                 event_.session_id,
                                 {},
                                 event_.sequence,
                                 std::move(payload)};
  }

 private:
  const WorkerPoolConfig& config_;
  const std::shared_ptr<std::atomic<bool>>& force_stop_requested_;
  OverloadStats* overload_stats_;
  util::BoundedBlockingQueue<service::SessionEvent>& inbox_;
  const service::SessionEvent& event_;
  std::vector<service::OutboundMessage> messages_;
  std::size_t emitted_messages_{};
  std::size_t emitted_bytes_{};
  bool disconnect_requested_{false};
  bool deferred_{false};
  std::shared_ptr<DeferredCompletionState> deferred_state_;
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
      config_.max_outbound_bytes_per_event == 0 ||
      config_.max_parked_events_per_session == 0) {
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

void WorkerPool::beginStop() {
  std::lock_guard<std::mutex> lock(sequence_mutex_);
  drain_requested_ = true;
  maybeCloseInboxForDrainLocked();
}

void WorkerPool::forceStop() {
  force_stop_requested_->store(true, std::memory_order_release);
  inbox_.close();
  outbox_.close();
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
  while (true) {
    service::SessionEvent event;
    std::shared_ptr<SessionSequenceState> session_state;
    std::size_t remaining_input;
    {
      // pop 순서대로 상태 참조를 확보해 늦게 실행된 이벤트도 종료 상태를 본다.
      std::lock_guard intake_lock(intake_mutex_);
      const auto pop_result = inbox_.pop(event);
      if (!pop_result.succeeded) {
        return;
      }
      remaining_input = pop_result.size;
      std::lock_guard sequence_lock(sequence_mutex_);
      auto& state = sequence_by_session_[event.session_id];
      if (state == nullptr) {
        state = std::make_shared<SessionSequenceState>();
      }
      session_state = state;
    }
    if (remaining_input == config_.inbound_low_watermark &&
        input_capacity_notifier_ != nullptr) {
      input_capacity_notifier_->notify();
    }

    if (force_stop_requested_->load(std::memory_order_acquire)) {
      return;
    }

    std::optional<service::SessionEvent> current(std::move(event));
    while (current.has_value()) {
      if (force_stop_requested_->load(std::memory_order_acquire)) {
        return;
      }

      const auto disposition = tryStartSessionTurn(*current, *session_state);
      if (disposition == SessionTurnDisposition::Parked) {
        break;
      }
      if (disposition == SessionTurnDisposition::Discarded) {
        std::lock_guard lock(sequence_mutex_);
        maybeCloseInboxForDrainLocked();
        break;
      }
      if (disposition == SessionTurnDisposition::InvalidSequence ||
          disposition == SessionTurnDisposition::ParkedLimit) {
        if (markSessionFailed(*session_state)) {
          if (overload_stats_ != nullptr) {
            if (disposition == SessionTurnDisposition::ParkedLimit) {
              overload_stats_->recordWorkerParkedLimitFailure();
            } else {
              overload_stats_->recordWorkerInvalidSequenceFailure();
            }
          }
          requestSessionDisconnect(current->session_id);
        }
        if (current->kind == service::SessionEventKind::Disconnected) {
          continue;
        }
        current = takePendingDisconnected(*session_state);
        continue;
      }

      if (current->kind == service::SessionEventKind::DeferredCompletion) {
        if (current->completion == nullptr || current->completion->failed) {
          if (markSessionFailed(*session_state)) {
            if (overload_stats_ != nullptr) {
              overload_stats_->recordWorkerDeferredFailure();
            }
            requestSessionDisconnect(current->session_id);
          }
        } else {
          WorkerSessionEventContext completion_context(
              config_, force_stop_requested_, overload_stats_, inbox_,
              *current);
          for (auto& message : current->completion->messages) {
            if (!completion_context.emit(std::move(message))) {
              break;
            }
          }
          for (auto& message : completion_context.release()) {
            if (!publishOutbound(std::move(message))) {
              break;
            }
          }
        }
        current = completeSessionTurn(*current, *session_state);
        continue;
      }

      bool deferred = false;
      std::optional<service::SessionEvent> inline_completion;
      if (!shouldSkipFailedSession(*current, *session_state)) {
        WorkerSessionEventContext context(config_, force_stop_requested_,
                                          overload_stats_, inbox_, *current);
        bool handler_succeeded = false;
        try {
          handler_.handle(*current, context);
          handler_succeeded = true;
        } catch (...) {
          if (overload_stats_ != nullptr) {
            overload_stats_->recordHandlerException();
          }
          if (markSessionFailed(*session_state)) {
            requestSessionDisconnect(current->session_id);
          }
        }
        if (handler_succeeded) {
          deferred = context.deferred();
          inline_completion = context.finishDeferredHandler();
          for (auto& message : context.release()) {
            if (!publishOutbound(std::move(message))) {
              break;
            }
          }
        }
      }
      if (deferred) {
        auto early_completion = markSessionDeferred(*current, *session_state);
        current = inline_completion.has_value() ? std::move(inline_completion)
                                                : std::move(early_completion);
      } else {
        current = completeSessionTurn(*current, *session_state);
      }
    }
  }
}

bool WorkerPool::shouldSkipFailedSession(const service::SessionEvent& event,
                                         SessionSequenceState& state) {
  if (event.kind == service::SessionEventKind::Disconnected) {
    return false;
  }
  std::lock_guard<std::mutex> lock(sequence_mutex_);
  return state.failed || state.closed;
}

bool WorkerPool::markSessionFailed(SessionSequenceState& state) {
  std::lock_guard<std::mutex> lock(sequence_mutex_);
  if (state.failed || state.closed) {
    return false;
  }
  state.failed = true;
  for (auto& [_, event] : state.parked) {
    if (event.kind == service::SessionEventKind::Disconnected) {
      state.pending_disconnected = std::move(event);
      break;
    }
  }
  state.parked.clear();
  return true;
}

std::optional<service::SessionEvent> WorkerPool::markSessionDeferred(
    const service::SessionEvent& event, SessionSequenceState& state) {
  std::lock_guard<std::mutex> lock(sequence_mutex_);
  if (!state.active || state.next_sequence != event.sequence) {
    throw std::logic_error("cannot defer inactive session event");
  }
  state.awaiting_completion = true;
  ++outstanding_deferred_;
  if (!state.early_completion.has_value()) {
    return std::nullopt;
  }
  auto completion = std::move(state.early_completion);
  state.early_completion.reset();
  return completion;
}

bool WorkerPool::publishOutbound(service::OutboundMessage message) {
  const auto push_result = outbox_.push(std::move(message));
  if (!push_result.succeeded) {
    return false;
  }
  if (overload_stats_ != nullptr) {
    overload_stats_->observeOutboundQueueSize(push_result.size);
  }
  if (outbound_notifier_ != nullptr) {
    outbound_notifier_->notify();
  }
  return true;
}

void WorkerPool::requestSessionDisconnect(std::uint64_t session_id) {
  static_cast<void>(publishOutbound(service::OutboundMessage{
      session_id, {}, service::OutboundMessageKind::DisconnectSession}));
}

WorkerPool::SessionTurnDisposition WorkerPool::tryStartSessionTurn(
    service::SessionEvent& event, SessionSequenceState& state) {
  std::lock_guard<std::mutex> lock(sequence_mutex_);
  if (state.closed) {
    return SessionTurnDisposition::Discarded;
  }

  if (event.kind == service::SessionEventKind::DeferredCompletion) {
    if (state.active && state.awaiting_completion &&
        state.next_sequence == event.sequence) {
      return SessionTurnDisposition::Process;
    }
    if (state.active && !state.awaiting_completion &&
        state.next_sequence == event.sequence &&
        !state.early_completion.has_value()) {
      state.early_completion = std::move(event);
      return SessionTurnDisposition::Parked;
    }
    return SessionTurnDisposition::InvalidSequence;
  }

  if (state.failed) {
    if (event.kind != service::SessionEventKind::Disconnected) {
      return SessionTurnDisposition::Discarded;
    }
    if (state.active) {
      state.pending_disconnected = std::move(event);
      return SessionTurnDisposition::Parked;
    }
    state.next_sequence = event.sequence;
    state.active = true;
    state.active_disconnect = true;
    return SessionTurnDisposition::Process;
  }

  if (!state.active && state.next_sequence == event.sequence) {
    state.active = true;
    state.active_disconnect =
        event.kind == service::SessionEventKind::Disconnected;
    return SessionTurnDisposition::Process;
  }

  if (event.sequence < state.next_sequence ||
      state.parked.contains(event.sequence)) {
    return SessionTurnDisposition::InvalidSequence;
  }
  if (state.parked.size() >= config_.max_parked_events_per_session) {
    return SessionTurnDisposition::ParkedLimit;
  }
  const auto [_, inserted] =
      state.parked.emplace(event.sequence, std::move(event));
  return inserted ? SessionTurnDisposition::Parked
                  : SessionTurnDisposition::InvalidSequence;
}

std::optional<service::SessionEvent> WorkerPool::completeSessionTurn(
    const service::SessionEvent& event, SessionSequenceState& state) {
  std::lock_guard<std::mutex> lock(sequence_mutex_);
  state.active = false;
  state.awaiting_completion = false;
  if (event.kind == service::SessionEventKind::DeferredCompletion &&
      outstanding_deferred_ > 0) {
    --outstanding_deferred_;
  }
  ++state.next_sequence;

  if (state.active_disconnect) {
    state.closed = true;
    sequence_by_session_.erase(event.session_id);
    maybeCloseInboxForDrainLocked();
    return std::nullopt;
  }

  if (state.failed) {
    auto disconnected = std::move(state.pending_disconnected);
    state.pending_disconnected.reset();
    if (!disconnected.has_value()) {
      maybeCloseInboxForDrainLocked();
    }
    return disconnected;
  }

  const auto next_it = state.parked.find(state.next_sequence);
  if (next_it == state.parked.end()) {
    maybeCloseInboxForDrainLocked();
    return std::nullopt;
  }
  auto next = std::move(next_it->second);
  state.parked.erase(next_it);
  return next;
}

std::optional<service::SessionEvent> WorkerPool::takePendingDisconnected(
    SessionSequenceState& state) {
  std::lock_guard lock(sequence_mutex_);
  if (state.active || state.closed) {
    return std::nullopt;
  }
  auto disconnected = std::move(state.pending_disconnected);
  state.pending_disconnected.reset();
  return disconnected;
}

void WorkerPool::maybeCloseInboxForDrainLocked() {
  if (!drain_requested_ || outstanding_deferred_ != 0 || inbox_.size() != 0) {
    return;
  }
  for (const auto& [_, state] : sequence_by_session_) {
    if (state->active || !state->parked.empty() ||
        state->early_completion.has_value() ||
        state->pending_disconnected.has_value()) {
      return;
    }
  }
  inbox_.close();
}

}  // namespace rss::net
