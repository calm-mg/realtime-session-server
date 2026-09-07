#include "rss/protocol/ProtocolVersion.h"

#include <algorithm>
#include <charconv>
#include <system_error>

#include "rss/protocol/ProtocolError.h"
#include "rss/protocol/StructuredPayload.h"

namespace rss::protocol {
namespace {
std::uint16_t parseVersion(std::string_view text) {
  std::uint16_t value{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || error != std::errc{} ||
      end != text.data() + text.size() || value == 0) {
    throw ProtocolError("invalid protocol version");
  }
  return value;
}

void validateRange(VersionRange range) {
  if (range.min_version == 0 || range.min_version > range.max_version) {
    throw ProtocolError("invalid protocol version range");
  }
}
}  // namespace

std::string encodeVersionRequest(VersionRange range) {
  validateRange(range);
  return StructuredPayloadBuilder{}
      .addField("min_version", std::to_string(range.min_version))
      .addField("max_version", std::to_string(range.max_version))
      .build();
}

VersionRange decodeVersionRequest(std::string_view payload) {
  const auto parsed = StructuredPayload::parse(payload);
  if (parsed.status().has_value() || parsed.fields().size() != 2) {
    throw ProtocolError("invalid version request fields");
  }
  const VersionRange range{parseVersion(parsed.requireField("min_version")),
                           parseVersion(parsed.requireField("max_version"))};
  validateRange(range);
  return range;
}

std::string encodeVersionResponse(std::uint16_t version) {
  if (version == 0) {
    throw ProtocolError("invalid protocol version");
  }
  return StructuredPayloadBuilder{"OK"}
      .addField("version", std::to_string(version))
      .build();
}

std::uint16_t decodeVersionResponse(std::string_view payload) {
  const auto parsed = StructuredPayload::parse(payload);
  if (parsed.status() != std::optional<std::string_view>{"OK"} ||
      parsed.fields().size() != 1) {
    throw ProtocolError("invalid version response fields");
  }
  return parseVersion(parsed.requireField("version"));
}

std::optional<std::uint16_t> negotiateVersion(VersionRange range) {
  validateRange(range);
  const auto lower = std::max(range.min_version, kMinProtocolVersion);
  const auto upper = std::min(range.max_version, kMaxProtocolVersion);
  if (lower > upper) {
    return std::nullopt;
  }
  return upper;
}

}  // namespace rss::protocol
