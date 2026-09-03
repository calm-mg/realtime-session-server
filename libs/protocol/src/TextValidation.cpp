#include "rss/protocol/TextValidation.h"

#include <cstddef>
#include <cstdint>

namespace rss::protocol {
namespace {

bool isContinuation(std::uint8_t byte) {
  return byte >= 0x80U && byte <= 0xbfU;
}

bool hasContinuationBytes(std::string_view text, std::size_t index,
                          std::size_t count) {
  if (text.size() - index <= count) {
    return false;
  }
  for (std::size_t offset = 1; offset <= count; ++offset) {
    if (!isContinuation(static_cast<std::uint8_t>(text[index + offset]))) {
      return false;
    }
  }
  return true;
}

bool isAsciiWhitespace(char character) {
  return character == ' ' || character == '\t' || character == '\n' ||
         character == '\r' || character == '\f' || character == '\v';
}

}  // namespace

bool isValidText(std::string_view text) noexcept {
  std::size_t index{};
  while (index < text.size()) {
    const auto lead = static_cast<std::uint8_t>(text[index]);
    if (lead <= 0x7fU) {
      if (lead <= 0x1fU || lead == 0x7fU) {
        return false;
      }
      ++index;
      continue;
    }

    if (lead >= 0xc2U && lead <= 0xdfU) {
      if (!hasContinuationBytes(text, index, 1)) {
        return false;
      }
      index += 2;
      continue;
    }

    if (lead >= 0xe0U && lead <= 0xefU) {
      if (!hasContinuationBytes(text, index, 2)) {
        return false;
      }
      const auto second = static_cast<std::uint8_t>(text[index + 1]);
      if ((lead == 0xe0U && second < 0xa0U) ||
          (lead == 0xedU && second > 0x9fU)) {
        return false;
      }
      index += 3;
      continue;
    }

    if (lead >= 0xf0U && lead <= 0xf4U) {
      if (!hasContinuationBytes(text, index, 3)) {
        return false;
      }
      const auto second = static_cast<std::uint8_t>(text[index + 1]);
      if ((lead == 0xf0U && second < 0x90U) ||
          (lead == 0xf4U && second > 0x8fU)) {
        return false;
      }
      index += 4;
      continue;
    }

    return false;
  }
  return true;
}

std::string_view trimAsciiWhitespace(std::string_view text) noexcept {
  std::size_t begin{};
  while (begin < text.size() && isAsciiWhitespace(text[begin])) {
    ++begin;
  }

  std::size_t end = text.size();
  while (end > begin && isAsciiWhitespace(text[end - 1])) {
    --end;
  }
  return text.substr(begin, end - begin);
}

}  // namespace rss::protocol
