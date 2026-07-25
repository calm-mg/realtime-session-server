#pragma once

#include <cstddef>
#include <thread>
#include <vector>

#include "rss/net/CompletionNotifier.h"
#include "rss/service/SessionEventHandler.h"
#include "rss/util/BlockingQueue.h"

namespace rss::net {

class WorkerPool {
 public:
  WorkerPool(util::BlockingQueue<service::SessionEvent>& inbox,
             util::BlockingQueue<service::OutboundMessage>& outbox,
             service::SessionEventHandler& handler,
             CompletionNotifier* completion_notifier = nullptr);
  ~WorkerPool();

  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  void start(std::size_t thread_count);
  void stop();

 private:
  void run();

  util::BlockingQueue<service::SessionEvent>& inbox_;
  util::BlockingQueue<service::OutboundMessage>& outbox_;
  service::SessionEventHandler& handler_;
  CompletionNotifier* completion_notifier_{nullptr};
  std::vector<std::thread> threads_;
};

}  // namespace rss::net
