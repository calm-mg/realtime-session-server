#pragma once

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

#include "rss/net/CompletionNotifier.h"
#include "rss/service/SessionEventHandler.h"
#include "rss/util/BoundedBlockingQueue.h"

namespace rss::net {

class WorkerPool {
 public:
  WorkerPool(util::BoundedBlockingQueue<service::SessionEvent>& inbox,
             util::BoundedBlockingQueue<service::OutboundMessage>& outbox,
             service::SessionEventHandler& handler,
             std::size_t inbound_low_watermark,
             CompletionNotifier* outbound_notifier = nullptr,
             CompletionNotifier* input_capacity_notifier = nullptr);
  ~WorkerPool() noexcept;

  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  void start(std::size_t thread_count);
  void beginStop();
  void forceStop();
  [[nodiscard]] bool finished() const;
  void join();

 private:
  void run();

  util::BoundedBlockingQueue<service::SessionEvent>& inbox_;
  util::BoundedBlockingQueue<service::OutboundMessage>& outbox_;
  service::SessionEventHandler& handler_;
  std::size_t inbound_low_watermark_;
  CompletionNotifier* outbound_notifier_{nullptr};
  CompletionNotifier* input_capacity_notifier_{nullptr};
  std::atomic<std::size_t> active_workers_{0};
  std::vector<std::thread> threads_;
};

}  // namespace rss::net
