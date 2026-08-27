#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rss::domain {

class UserId {
 public:
  using Bytes = std::array<std::uint8_t, 16>;

  constexpr UserId() = default;
  explicit constexpr UserId(Bytes bytes) : bytes_(bytes) {}

  [[nodiscard]] static std::optional<UserId> parse(std::string_view text);
  [[nodiscard]] std::string toString() const;
  [[nodiscard]] constexpr const Bytes& bytes() const noexcept { return bytes_; }

  auto operator<=>(const UserId&) const = default;

 private:
  Bytes bytes_{};
};

}  // namespace rss::domain
