#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

#include "ShutdownSignalMonitor.h"
#include "rss/net/TcpServer.h"
#include "rss/persistence/postgres/PostgresExecutor.h"
#include "rss/persistence/postgres/PostgresUserRepository.h"
#include "rss/service/MessageRouter.h"
#include "rss/service/RoomService.h"

namespace {

std::uint16_t parsePort(const char* value) {
  const auto port = std::stoi(value);
  if (port <= 0 || port > 65535) {
    throw std::runtime_error("port must be in range 1..65535");
  }
  return static_cast<std::uint16_t>(port);
}

std::string requiredEnvironment(const char* name) {
  const auto* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    throw std::runtime_error(std::string(name) + " is required");
  }
  return value;
}

std::size_t sizeEnvironment(const char* name, std::size_t fallback) {
  const auto* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return fallback;
  }
  if (!std::all_of(value, value + std::char_traits<char>::length(value),
                   [](char character) {
                     return character >= '0' && character <= '9';
                   })) {
    throw std::runtime_error(std::string(name) + " must be a positive integer");
  }
  std::size_t parsed_characters{};
  const auto parsed = std::stoull(value, &parsed_characters);
  if (parsed == 0 || value[parsed_characters] != '\0' ||
      parsed > std::numeric_limits<std::size_t>::max()) {
    throw std::runtime_error(std::string(name) + " must be a positive integer");
  }
  return static_cast<std::size_t>(parsed);
}

}  // namespace

int main(int argc, char** argv) {
  rss::net::ServerConfig config;
  if (argc >= 2) {
    config.host = argv[1];
  }
  if (argc >= 3) {
    config.port = parsePort(argv[2]);
  }
  if (argc >= 4) {
    config.worker_count = static_cast<std::size_t>(std::stoul(argv[3]));
  } else {
    config.worker_count = std::max(1U, std::thread::hardware_concurrency());
  }

  try {
    const auto shutdown_signals = rss::server::blockShutdownSignals();
    const auto database_url = requiredEnvironment("RSS_DATABASE_URL");
    const auto database_workers = sizeEnvironment("RSS_DB_WORKERS", 2);
    const auto database_queue_capacity =
        sizeEnvironment("RSS_DB_QUEUE_CAPACITY", 1024);

    rss::persistence::postgres::PostgresExecutor executor(
        database_url, database_workers, database_queue_capacity);
    executor.start();
    rss::persistence::postgres::PostgresUserRepository users(executor);
    rss::service::RoomService rooms;
    rss::service::MessageRouter router(rooms, users);

    rss::net::TcpServer server(config, &router);
    rss::server::ShutdownSignalMonitor signal_monitor(shutdown_signals, server);
    try {
      server.run();
      signal_monitor.throwIfFailed();
    } catch (...) {
      executor.stop();
      throw;
    }
    executor.stop();
  } catch (const std::exception& ex) {
    std::cerr << "server failed: " << ex.what() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
