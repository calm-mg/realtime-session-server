#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rss::protocol {

std::string encodeStructuredValue(std::string_view value);
std::string decodeStructuredValue(std::string_view value);

using StructuredField = std::pair<std::string, std::string>;

class StructuredPayload final {
 public:
  static StructuredPayload parse(std::string_view payload);

  [[nodiscard]] std::optional<std::string_view> status() const noexcept;
  [[nodiscard]] std::optional<std::string_view> field(
      std::string_view key) const noexcept;
  [[nodiscard]] std::string_view requireField(std::string_view key) const;
  [[nodiscard]] const std::vector<StructuredField>& fields() const noexcept;

 private:
  std::optional<std::string> status_;
  std::vector<StructuredField> fields_;
};

class StructuredPayloadBuilder final {
 public:
  StructuredPayloadBuilder() = default;
  explicit StructuredPayloadBuilder(std::string_view status);

  StructuredPayloadBuilder& addField(std::string_view key,
                                     std::string_view value);
  [[nodiscard]] std::string build() const;

 private:
  std::optional<std::string> status_;
  std::vector<StructuredField> fields_;
};

}  // namespace rss::protocol
