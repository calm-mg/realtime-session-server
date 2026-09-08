#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "rss/net/OverloadStats.h"

namespace rss::observability {

struct SocketErrorDiagnostic {
  std::uint64_t session_id{};
  int fd{-1};
  std::string_view operation;
  int error_code{};
  std::uint32_t epoll_events{};
  std::size_t pending_write_bytes{};
};

[[nodiscard]] std::string formatSocketError(
    std::int64_t timestamp_unix_ms, const SocketErrorDiagnostic& diagnostic);

enum class SnapshotPhase { Periodic, Final };

[[nodiscard]] std::int64_t currentUnixTimeMilliseconds() noexcept;

[[nodiscard]] std::string formatServerStarted(std::int64_t timestamp_unix_ms,
                                              std::string_view host,
                                              std::uint16_t port,
                                              std::size_t worker_count);

[[nodiscard]] std::string formatOverloadSnapshot(
    std::int64_t timestamp_unix_ms, SnapshotPhase phase,
    const net::OverloadSnapshot& snapshot);

[[nodiscard]] std::string formatServerStopped(std::int64_t timestamp_unix_ms);

[[nodiscard]] std::string formatServerFailed(std::int64_t timestamp_unix_ms,
                                             std::string_view message);

}  // namespace rss::observability
