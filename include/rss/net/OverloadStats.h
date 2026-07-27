#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace rss::net {

struct OverloadSnapshot {
  std::uint64_t read_pauses{};
  std::uint64_t read_resumes{};
  std::uint64_t inbound_queue_full{};
  std::uint64_t outbound_budget_rejections{};
  std::uint64_t slow_client_disconnects{};
  std::uint64_t rejected_connections{};
  std::size_t max_inbound_queue_size{};
  std::size_t max_outbound_queue_size{};
  std::size_t max_session_pending_write_bytes{};
  std::size_t current_inbound_queue_size{};
  std::size_t current_outbound_queue_size{};
  std::size_t outbound_queue_waiting_producers{};
  std::size_t current_sessions{};
  bool outbound_queue_closed{};
};

class OverloadStats {
 public:
  void recordReadPause() noexcept {
    read_pauses_.fetch_add(1, std::memory_order_relaxed);
  }

  void recordReadResume() noexcept {
    read_resumes_.fetch_add(1, std::memory_order_relaxed);
  }

  void recordInboundQueueFull() noexcept {
    inbound_queue_full_.fetch_add(1, std::memory_order_relaxed);
  }

  void recordOutboundBudgetRejection() noexcept {
    outbound_budget_rejections_.fetch_add(1, std::memory_order_relaxed);
  }

  void recordSlowClientDisconnect() noexcept {
    slow_client_disconnects_.fetch_add(1, std::memory_order_relaxed);
  }

  void recordRejectedConnection() noexcept {
    rejected_connections_.fetch_add(1, std::memory_order_relaxed);
  }

  void observeInboundQueueSize(std::size_t size) noexcept {
    updateMaximum(max_inbound_queue_size_, size);
  }

  void observeOutboundQueueSize(std::size_t size) noexcept {
    updateMaximum(max_outbound_queue_size_, size);
  }

  void observeSessionPendingWriteBytes(std::size_t size) noexcept {
    updateMaximum(max_session_pending_write_bytes_, size);
  }

  [[nodiscard]] OverloadSnapshot snapshot(
      std::size_t current_inbound_queue_size,
      std::size_t current_outbound_queue_size,
      std::size_t current_sessions) const noexcept {
    return OverloadSnapshot{
        .read_pauses = read_pauses_.load(std::memory_order_relaxed),
        .read_resumes = read_resumes_.load(std::memory_order_relaxed),
        .inbound_queue_full =
            inbound_queue_full_.load(std::memory_order_relaxed),
        .outbound_budget_rejections =
            outbound_budget_rejections_.load(std::memory_order_relaxed),
        .slow_client_disconnects =
            slow_client_disconnects_.load(std::memory_order_relaxed),
        .rejected_connections =
            rejected_connections_.load(std::memory_order_relaxed),
        .max_inbound_queue_size =
            max_inbound_queue_size_.load(std::memory_order_relaxed),
        .max_outbound_queue_size =
            max_outbound_queue_size_.load(std::memory_order_relaxed),
        .max_session_pending_write_bytes =
            max_session_pending_write_bytes_.load(std::memory_order_relaxed),
        .current_inbound_queue_size = current_inbound_queue_size,
        .current_outbound_queue_size = current_outbound_queue_size,
        .outbound_queue_waiting_producers = 0,
        .current_sessions = current_sessions,
        .outbound_queue_closed = false,
    };
  }

 private:
  static void updateMaximum(std::atomic<std::size_t>& maximum,
                            std::size_t value) noexcept {
    auto observed = maximum.load(std::memory_order_relaxed);
    while (observed < value && !maximum.compare_exchange_weak(
                                   observed, value, std::memory_order_relaxed,
                                   std::memory_order_relaxed)) {
    }
  }

  std::atomic<std::uint64_t> read_pauses_{0};
  std::atomic<std::uint64_t> read_resumes_{0};
  std::atomic<std::uint64_t> inbound_queue_full_{0};
  std::atomic<std::uint64_t> outbound_budget_rejections_{0};
  std::atomic<std::uint64_t> slow_client_disconnects_{0};
  std::atomic<std::uint64_t> rejected_connections_{0};
  std::atomic<std::size_t> max_inbound_queue_size_{0};
  std::atomic<std::size_t> max_outbound_queue_size_{0};
  std::atomic<std::size_t> max_session_pending_write_bytes_{0};
};

}  // namespace rss::net
