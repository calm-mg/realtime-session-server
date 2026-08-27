#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
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
  struct SessionSequenceState {
    std::uint64_t next_sequence{};
    std::size_t waiting_workers{};
    bool active{};
    bool failed{};
  };

  void run();
  [[nodiscard]] bool waitForSessionTurn(const service::SessionEvent& event);
  [[nodiscard]] bool shouldSkipFailedSession(
      const service::SessionEvent& event);
  [[nodiscard]] bool markSessionFailed(std::uint64_t session_id);
  [[nodiscard]] bool publishOutbound(service::OutboundMessage message);
  void requestSessionDisconnect(std::uint64_t session_id);
  void completeSessionTurn(const service::SessionEvent& event);

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
  std::condition_variable sequence_changed_;
  std::unordered_map<std::uint64_t, SessionSequenceState> sequence_by_session_;
  std::vector<std::thread> threads_;
};

}  // namespace rss::net
