#pragma once

#include <signal.h>

#include <atomic>
#include <thread>

#include "rss/net/TcpServer.h"

namespace rss::server {

[[nodiscard]] sigset_t blockShutdownSignals();

class ShutdownSignalMonitor {
 public:
  ShutdownSignalMonitor(sigset_t signals, net::TcpServer& server);
  ~ShutdownSignalMonitor();

  ShutdownSignalMonitor(const ShutdownSignalMonitor&) = delete;
  ShutdownSignalMonitor& operator=(const ShutdownSignalMonitor&) = delete;

  void throwIfFailed() const;

 private:
  void waitLoop();

  sigset_t signals_{};
  net::TcpServer& server_;
  std::atomic<bool> stopping_{false};
  std::atomic<int> wait_error_{0};
  std::thread thread_;
};

}  // namespace rss::server
