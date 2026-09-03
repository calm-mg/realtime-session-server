#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "rss/net/OverloadStats.h"

namespace rss::observability {

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
