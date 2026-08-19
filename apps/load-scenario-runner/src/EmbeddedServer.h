#pragma once

#include <chrono>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>

#include "rss/net/OverloadStats.h"
#include "rss/net/ServerConfig.h"
#include "rss/net/TcpServer.h"

namespace rss::tools {

class EmbeddedServer {
 public:
  explicit EmbeddedServer(rss::net::ServerConfig config);
  ~EmbeddedServer();

  EmbeddedServer(const EmbeddedServer&) = delete;
  EmbeddedServer& operator=(const EmbeddedServer&) = delete;

  void start(std::chrono::milliseconds timeout);
  void stop();
  [[nodiscard]] std::uint16_t port() const noexcept;
  [[nodiscard]] rss::net::OverloadSnapshot snapshot() const;

 private:
  rss::net::TcpServer server_;
  std::thread thread_;
  std::exception_ptr failure_;
  std::mutex failure_mutex_;
  bool started_{};
};

}  // namespace rss::tools
