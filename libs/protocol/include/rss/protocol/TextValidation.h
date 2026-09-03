#pragma once

#include <string_view>

namespace rss::protocol {

[[nodiscard]] bool isValidText(std::string_view text) noexcept;
[[nodiscard]] std::string_view trimAsciiWhitespace(
    std::string_view text) noexcept;

}  // namespace rss::protocol
