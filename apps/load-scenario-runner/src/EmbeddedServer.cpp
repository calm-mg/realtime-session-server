#include "EmbeddedServer.h"

#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace rss::tools {
namespace {

rss::net::ServerConfig embeddedServerConfig(rss::net::ServerConfig config) {
  config.emit_startup_diagnostic = false;
  return config;
}

}  // namespace

EmbeddedServer::EmbeddedServer(rss::net::ServerConfig config)
    : server_(embeddedServerConfig(std::move(config))) {}

EmbeddedServer::~EmbeddedServer() {
  try {
    stop();
  } catch (...) {
  }
}

void EmbeddedServer::start(std::chrono::milliseconds timeout) {
  if (started_) {
    throw std::logic_error("embedded server is already started");
  }
  started_ = true;

  thread_ = std::thread([this] {
    try {
      server_.run();
    } catch (...) {
      std::lock_guard<std::mutex> lock(failure_mutex_);
      failure_ = std::current_exception();
    }
  });

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    std::exception_ptr failure;
    {
      std::lock_guard<std::mutex> lock(failure_mutex_);
      failure = failure_;
    }
    if (failure != nullptr) {
      thread_.join();
      std::rethrow_exception(failure);
    }
    if (server_.boundPort() != 0) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  stop();
  throw std::runtime_error("embedded server did not start before timeout");
}

void EmbeddedServer::stop() {
  server_.stop();
  if (thread_.joinable()) {
    thread_.join();
  }

  std::exception_ptr failure;
  {
    std::lock_guard<std::mutex> lock(failure_mutex_);
    failure = failure_;
  }
  if (failure != nullptr) {
    std::rethrow_exception(failure);
  }
}

std::uint16_t EmbeddedServer::port() const noexcept {
  return server_.boundPort();
}

rss::net::OverloadSnapshot EmbeddedServer::snapshot() const {
  return server_.overloadSnapshot();
}

}  // namespace rss::tools
