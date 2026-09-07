#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace rss::net {

enum class DisconnectReason {
  PeerClosed,
  SocketError,
  ProtocolError,
  IdleTimeout,
  WorkerRequested,
  PendingWriteLimit,
  CloseAfterFlush,
  Shutdown,
};

struct OverloadSnapshot {
  std::uint64_t read_pauses{};
  std::uint64_t read_resumes{};
  std::uint64_t inbound_queue_full{};
  std::uint64_t outbound_budget_rejections{};
  std::uint64_t handler_exceptions{};
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
  // 실제 TCP 세션 제거 시 최초 원인 하나만 누적한다.
  std::uint64_t disconnect_peer_closed{};
  std::uint64_t disconnect_socket_error{};
  std::uint64_t disconnect_protocol_error{};
  std::uint64_t disconnect_idle_timeout{};
  std::uint64_t disconnect_worker_requested{};
  std::uint64_t disconnect_pending_write_limit{};
  std::uint64_t disconnect_close_after_flush{};
  std::uint64_t disconnect_shutdown{};
  // Worker의 최초 실패 전이를 센다. 실제 I/O 연결 종료 수와 별개다.
  std::uint64_t worker_parked_limit_failures{};
  std::uint64_t worker_invalid_sequence_failures{};
  std::uint64_t worker_deferred_failures{};
};

class OverloadStats {
 public:
  void recordDisconnect(DisconnectReason reason) noexcept {
    switch (reason) {
      case DisconnectReason::PeerClosed:
        disconnect_peer_closed_.fetch_add(1, std::memory_order_relaxed);
        break;
      case DisconnectReason::SocketError:
        disconnect_socket_error_.fetch_add(1, std::memory_order_relaxed);
        break;
      case DisconnectReason::ProtocolError:
        disconnect_protocol_error_.fetch_add(1, std::memory_order_relaxed);
        break;
      case DisconnectReason::IdleTimeout:
        disconnect_idle_timeout_.fetch_add(1, std::memory_order_relaxed);
        break;
      case DisconnectReason::WorkerRequested:
        disconnect_worker_requested_.fetch_add(1, std::memory_order_relaxed);
        break;
      case DisconnectReason::PendingWriteLimit:
        disconnect_pending_write_limit_.fetch_add(1, std::memory_order_relaxed);
        break;
      case DisconnectReason::CloseAfterFlush:
        disconnect_close_after_flush_.fetch_add(1, std::memory_order_relaxed);
        break;
      case DisconnectReason::Shutdown:
        disconnect_shutdown_.fetch_add(1, std::memory_order_relaxed);
        break;
    }
  }

  void recordWorkerParkedLimitFailure() noexcept {
    worker_parked_limit_failures_.fetch_add(1, std::memory_order_relaxed);
  }

  void recordWorkerInvalidSequenceFailure() noexcept {
    worker_invalid_sequence_failures_.fetch_add(1, std::memory_order_relaxed);
  }

  void recordWorkerDeferredFailure() noexcept {
    worker_deferred_failures_.fetch_add(1, std::memory_order_relaxed);
  }

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

  void recordHandlerException() noexcept {
    handler_exceptions_.fetch_add(1, std::memory_order_relaxed);
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
        .handler_exceptions =
            handler_exceptions_.load(std::memory_order_relaxed),
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
        .disconnect_peer_closed =
            disconnect_peer_closed_.load(std::memory_order_relaxed),
        .disconnect_socket_error =
            disconnect_socket_error_.load(std::memory_order_relaxed),
        .disconnect_protocol_error =
            disconnect_protocol_error_.load(std::memory_order_relaxed),
        .disconnect_idle_timeout =
            disconnect_idle_timeout_.load(std::memory_order_relaxed),
        .disconnect_worker_requested =
            disconnect_worker_requested_.load(std::memory_order_relaxed),
        .disconnect_pending_write_limit =
            disconnect_pending_write_limit_.load(std::memory_order_relaxed),
        .disconnect_close_after_flush =
            disconnect_close_after_flush_.load(std::memory_order_relaxed),
        .disconnect_shutdown =
            disconnect_shutdown_.load(std::memory_order_relaxed),
        .worker_parked_limit_failures =
            worker_parked_limit_failures_.load(std::memory_order_relaxed),
        .worker_invalid_sequence_failures =
            worker_invalid_sequence_failures_.load(std::memory_order_relaxed),
        .worker_deferred_failures =
            worker_deferred_failures_.load(std::memory_order_relaxed),

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

  std::atomic<std::uint64_t> disconnect_peer_closed_{0};
  std::atomic<std::uint64_t> disconnect_socket_error_{0};
  std::atomic<std::uint64_t> disconnect_protocol_error_{0};
  std::atomic<std::uint64_t> disconnect_idle_timeout_{0};
  std::atomic<std::uint64_t> disconnect_worker_requested_{0};
  std::atomic<std::uint64_t> disconnect_pending_write_limit_{0};
  std::atomic<std::uint64_t> disconnect_close_after_flush_{0};
  std::atomic<std::uint64_t> disconnect_shutdown_{0};
  std::atomic<std::uint64_t> worker_parked_limit_failures_{0};
  std::atomic<std::uint64_t> worker_invalid_sequence_failures_{0};
  std::atomic<std::uint64_t> worker_deferred_failures_{0};
  std::atomic<std::uint64_t> read_pauses_{0};
  std::atomic<std::uint64_t> read_resumes_{0};
  std::atomic<std::uint64_t> inbound_queue_full_{0};
  std::atomic<std::uint64_t> outbound_budget_rejections_{0};
  std::atomic<std::uint64_t> handler_exceptions_{0};
  std::atomic<std::uint64_t> slow_client_disconnects_{0};
  std::atomic<std::uint64_t> rejected_connections_{0};
  std::atomic<std::size_t> max_inbound_queue_size_{0};
  std::atomic<std::size_t> max_outbound_queue_size_{0};
  std::atomic<std::size_t> max_session_pending_write_bytes_{0};
};

}  // namespace rss::net
