#include "rss/domain/UserId.h"

#include <array>
#include <cstddef>

namespace rss::domain {
namespace {

constexpr std::array<std::size_t, 4> kDashPositions{8, 13, 18, 23};

[[nodiscard]] constexpr bool isDashPosition(std::size_t position) noexcept {
  for (const auto dash_position : kDashPositions) {
    if (position == dash_position) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] constexpr std::optional<std::uint8_t> decodeHex(
    char character) noexcept {
  if (character >= '0' && character <= '9') {
    return static_cast<std::uint8_t>(character - '0');
  }
  if (character >= 'a' && character <= 'f') {
    return static_cast<std::uint8_t>(character - 'a' + 10);
  }
  return std::nullopt;
}

}  // namespace

std::optional<UserId> UserId::parse(std::string_view text) {
  if (text.size() != 36) {
    return std::nullopt;
  }

  Bytes bytes{};
  std::size_t nibble_index{};
  for (std::size_t text_index = 0; text_index < text.size(); ++text_index) {
    if (isDashPosition(text_index)) {
      if (text[text_index] != '-') {
        return std::nullopt;
      }
      continue;
    }

    const auto nibble = decodeHex(text[text_index]);
    if (!nibble.has_value()) {
      return std::nullopt;
    }
    const auto byte_index = nibble_index / 2;
    if (nibble_index % 2 == 0) {
      bytes[byte_index] = static_cast<std::uint8_t>(*nibble << 4);
    } else {
      bytes[byte_index] =
          static_cast<std::uint8_t>(bytes[byte_index] | *nibble);
    }
    ++nibble_index;
  }

  return UserId{bytes};
}

std::string UserId::toString() const {
  constexpr char kHexDigits[] = "0123456789abcdef";
  std::string result;
  result.reserve(36);
  for (const auto byte : bytes_) {
    if (isDashPosition(result.size())) {
      result.push_back('-');
    }
    result.push_back(kHexDigits[byte >> 4]);
    result.push_back(kHexDigits[byte & 0x0f]);
  }
  return result;
}

}  // namespace rss::domain
