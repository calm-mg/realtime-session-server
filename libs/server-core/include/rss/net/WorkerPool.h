#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include "rss/net/CompletionNotifier.h"
#include "rss/net/OverloadStats.h"
#include "rss/service/SessionEventHandler.h"
#include "rss/util/BoundedBlockingQueue.h"

namespace rss::net {

struct WorkerPoolConfig {
  std::size_t inbound_low_watermark{};
  std::size_t max_outbound_messages_per_event{};
  std::size_t max_outbound_bytes_per_event{};
  std::size_t max_parked_events_per_session{32};
};

class WorkerPool {
 public:
  WorkerPool(util::BoundedBlockingQueue<service::SessionEvent>& inbox,
             util::BoundedBlockingQueue<service::OutboundMessage>& outbox,
             service::SessionEventHandler& handler, WorkerPoolConfig config,
             CompletionNotifier* outbound_notifier = nullptr,
             CompletionNotifier* input_capacity_notifier = nullptr,
             OverloadStats* overload_stats = nullptr);
  ~WorkerPool() noexcept;

  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  void start(std::size_t thread_count);
  void beginStop();
  void forceStop();
  [[nodiscard]] bool finished() const;
  void join();

 private:
  enum class SessionTurnDisposition {
    Process,
    Parked,
    Rejected,
  };

  struct SessionSequenceState {
    std::uint64_t next_sequence{};
    bool active{};
    bool awaiting_completion{};
    bool failed{};
    std::map<std::uint64_t, service::SessionEvent> parked;
    std::optional<service::SessionEvent> early_completion;
  };

  void run();
  [[nodiscard]] SessionTurnDisposition tryStartSessionTurn(
      service::SessionEvent& event);
  [[nodiscard]] bool shouldSkipFailedSession(
      const service::SessionEvent& event);
  [[nodiscard]] bool markSessionFailed(std::uint64_t session_id);
  [[nodiscard]] std::optional<service::SessionEvent> markSessionDeferred(
      const service::SessionEvent& event);
  [[nodiscard]] bool publishOutbound(service::OutboundMessage message);
  void requestSessionDisconnect(std::uint64_t session_id);
  [[nodiscard]] std::optional<service::SessionEvent> completeSessionTurn(
      const service::SessionEvent& event);
  void maybeCloseInboxForDrainLocked();

  util::BoundedBlockingQueue<service::SessionEvent>& inbox_;
  util::BoundedBlockingQueue<service::OutboundMessage>& outbox_;
  service::SessionEventHandler& handler_;
  WorkerPoolConfig config_;
  CompletionNotifier* outbound_notifier_{nullptr};
  CompletionNotifier* input_capacity_notifier_{nullptr};
  OverloadStats* overload_stats_{nullptr};
  std::atomic<std::size_t> active_workers_{0};
  std::atomic<bool> force_stop_requested_{false};
  std::mutex sequence_mutex_;
  std::unordered_map<std::uint64_t, SessionSequenceState> sequence_by_session_;
  std::size_t outstanding_deferred_{};
  bool drain_requested_{false};
  std::vector<std::thread> threads_;
};

}  // namespace rss::net
