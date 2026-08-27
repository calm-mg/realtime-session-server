#include "rss/persistence/postgres/PostgresExecutor.h"

#include <libpq-fe.h>

#include <stdexcept>
#include <utility>

namespace rss::persistence::postgres {
namespace {

[[nodiscard]] bool configureConnection(PGconn* connection) {
  if (PQstatus(connection) != CONNECTION_OK) {
    return false;
  }
  auto* result = PQexec(connection, "SET statement_timeout = '5s'");
  const bool configured =
      result != nullptr && PQresultStatus(result) == PGRES_COMMAND_OK;
  if (result != nullptr) {
    PQclear(result);
  }
  return configured;
}

void finishConnections(std::vector<pg_conn*>& connections) noexcept {
  for (auto* connection : connections) {
    PQfinish(connection);
  }
  connections.clear();
}

}  // namespace

PostgresExecutor::PostgresExecutor(std::string connection_string,
                                   std::size_t worker_count,
                                   std::size_t queue_capacity)
    : connection_string_(std::move(connection_string)),
      worker_count_(worker_count),
      tasks_(queue_capacity) {
  if (worker_count_ == 0) {
    throw std::invalid_argument(
        "PostgreSQL worker count must be greater than zero");
  }
}

PostgresExecutor::~PostgresExecutor() noexcept { stop(); }

void PostgresExecutor::start() {
  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  if (started_ || tasks_.closed()) {
    throw std::logic_error("PostgreSQL executor cannot be started");
  }

  std::vector<pg_conn*> connections;
  connections.reserve(worker_count_);
  for (std::size_t index = 0; index < worker_count_; ++index) {
    auto* connection = PQconnectdb(connection_string_.c_str());
    if (connection == nullptr || !configureConnection(connection)) {
      if (connection != nullptr) {
        PQfinish(connection);
      }
      finishConnections(connections);
      throw std::runtime_error("failed to start PostgreSQL executor");
    }
    connections.push_back(connection);
  }

  connections_ = std::move(connections);
  accepting_.store(true, std::memory_order_release);
  try {
    threads_.reserve(worker_count_);
    for (auto* connection : connections_) {
      threads_.emplace_back([this, connection] { run(connection); });
    }
  } catch (...) {
    accepting_.store(false, std::memory_order_release);
    tasks_.close();
    for (auto& thread : threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }
    threads_.clear();
    finishConnections(connections_);
    throw;
  }
  started_ = true;
}

bool PostgresExecutor::submit(Task task) {
  if (!accepting_.load(std::memory_order_acquire)) {
    return false;
  }
  return tasks_.tryPush(std::move(task)).succeeded;
}

void PostgresExecutor::stop() noexcept {
  std::vector<std::thread> threads;
  std::vector<pg_conn*> connections;
  {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    accepting_.store(false, std::memory_order_release);
    if (!started_ && threads_.empty() && connections_.empty()) {
      return;
    }
    tasks_.close();
    threads = std::move(threads_);
    connections = std::move(connections_);
    started_ = false;
  }

  for (auto& thread : threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  finishConnections(connections);
}

bool PostgresExecutor::isRunning() const noexcept {
  return accepting_.load(std::memory_order_acquire);
}

void PostgresExecutor::run(pg_conn* connection) noexcept {
  Task task;
  while (tasks_.pop(task).succeeded) {
    if (PQstatus(connection) != CONNECTION_OK) {
      PQreset(connection);
      static_cast<void>(configureConnection(connection));
    }
    try {
      task(connection);
    } catch (...) {
      // Repository tasks own their callback error boundary. Keep the executor
      // alive if an unrelated task violates that contract.
    }
  }
}

}  // namespace rss::persistence::postgres
