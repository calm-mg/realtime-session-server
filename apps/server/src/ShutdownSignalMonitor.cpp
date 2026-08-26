#include "ShutdownSignalMonitor.h"

#include <pthread.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace rss::server {
namespace {

std::runtime_error signalError(const char* operation, int error) {
  return std::runtime_error(std::string(operation) + ": " +
                            std::strerror(error));
}

}  // namespace

sigset_t blockShutdownSignals() {
  sigset_t signals{};
  if (::sigemptyset(&signals) != 0 || ::sigaddset(&signals, SIGINT) != 0 ||
      ::sigaddset(&signals, SIGTERM) != 0) {
    throw signalError("failed to build shutdown signal set", errno);
  }

  const auto result = ::pthread_sigmask(SIG_BLOCK, &signals, nullptr);
  if (result != 0) {
    throw signalError("pthread_sigmask failed", result);
  }
  return signals;
}

ShutdownSignalMonitor::ShutdownSignalMonitor(sigset_t signals,
                                             net::TcpServer& server)
    : signals_(signals), server_(server), thread_([this] { waitLoop(); }) {}

ShutdownSignalMonitor::~ShutdownSignalMonitor() {
  stopping_.store(true, std::memory_order_release);
  if (!thread_.joinable()) {
    return;
  }
  static_cast<void>(::pthread_kill(thread_.native_handle(), SIGTERM));
  thread_.join();
}

void ShutdownSignalMonitor::throwIfFailed() const {
  const auto error = wait_error_.load(std::memory_order_acquire);
  if (error != 0) {
    throw signalError("sigwait failed", error);
  }
}

void ShutdownSignalMonitor::waitLoop() {
  while (!stopping_.load(std::memory_order_acquire)) {
    int signal_number{};
    const auto result = ::sigwait(&signals_, &signal_number);
    if (result != 0) {
      wait_error_.store(result, std::memory_order_release);
      server_.stop();
      return;
    }
    if (stopping_.load(std::memory_order_acquire)) {
      return;
    }
    server_.stop();
  }
}

}  // namespace rss::server
