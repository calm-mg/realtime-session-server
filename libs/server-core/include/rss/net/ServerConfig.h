#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace rss::net {

struct ServerConfig {
  std::string host{"0.0.0.0"};
  std::uint16_t port{7777};
  std::size_t worker_count{4};
  int backlog{512};
  int max_events{256};
  std::chrono::seconds idle_timeout{60};
  std::size_t inbound_queue_capacity{4096};
  std::size_t inbound_high_watermark{3072};
  std::size_t inbound_low_watermark{2048};
  std::size_t outbound_queue_capacity{4096};
  std::size_t max_outbound_messages_per_event{10000};
  std::size_t max_outbound_bytes_per_event{40U * 1024U * 1024U};
  std::size_t max_pending_write_bytes{1024 * 1024};
  std::size_t max_sessions{10000};
  std::chrono::seconds graceful_shutdown_timeout{5};
  bool emit_startup_diagnostic{true};

  void validate() const {
    if (host.empty()) {
      throw std::invalid_argument("host must not be empty");
    }
    if (worker_count == 0) {
      throw std::invalid_argument("worker count must be positive");
    }
    if (backlog <= 0) {
      throw std::invalid_argument("backlog must be positive");
    }
    if (max_events <= 0) {
      throw std::invalid_argument("maximum events must be positive");
    }
    if (idle_timeout <= std::chrono::seconds::zero()) {
      throw std::invalid_argument("idle timeout must be positive");
    }
    if (inbound_queue_capacity == 0) {
      throw std::invalid_argument("inbound queue capacity must be positive");
    }
    if (inbound_low_watermark == 0 ||
        inbound_low_watermark >= inbound_high_watermark ||
        inbound_high_watermark > inbound_queue_capacity) {
      throw std::invalid_argument("invalid inbound queue watermarks");
    }
    if (outbound_queue_capacity == 0) {
      throw std::invalid_argument("outbound queue capacity must be positive");
    }
    if (max_outbound_messages_per_event == 0) {
      throw std::invalid_argument(
          "maximum outbound messages per event must be positive");
    }
    if (max_outbound_bytes_per_event == 0) {
      throw std::invalid_argument(
          "maximum outbound bytes per event must be positive");
    }
    if (max_pending_write_bytes == 0) {
      throw std::invalid_argument(
          "maximum pending write bytes must be positive");
    }
    if (max_sessions == 0) {
      throw std::invalid_argument("maximum sessions must be positive");
    }
    if (graceful_shutdown_timeout <= std::chrono::seconds::zero()) {
      throw std::invalid_argument("graceful shutdown timeout must be positive");
    }
  }
};

}  // namespace rss::net
