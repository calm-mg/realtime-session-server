#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rss::protocol {

inline constexpr std::uint16_t kMinProtocolVersion = 1;
inline constexpr std::uint16_t kMaxProtocolVersion = 1;
inline constexpr std::chrono::milliseconds kVersionNegotiationTimeout{5000};

struct VersionRange {
  std::uint16_t min_version;
  std::uint16_t max_version;
};

std::string encodeVersionRequest(VersionRange range);
VersionRange decodeVersionRequest(std::string_view payload);
std::string encodeVersionResponse(std::uint16_t version);
std::uint16_t decodeVersionResponse(std::string_view payload);
std::optional<std::uint16_t> negotiateVersion(VersionRange range);

}  // namespace rss::protocol
