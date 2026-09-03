#include "rss/protocol/StructuredPayload.h"

#include <algorithm>

#include "rss/protocol/ProtocolError.h"
#include "rss/protocol/TextValidation.h"

namespace rss::protocol {
namespace {

bool isValidKey(std::string_view key) {
  return !key.empty() && std::ranges::all_of(key, [](unsigned char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_';
  });
}

int hexValue(char character) {
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'A' && character <= 'F') {
    return character - 'A' + 10;
  }
  if (character >= 'a' && character <= 'f') {
    return character - 'a' + 10;
  }
  return -1;
}

bool containsKey(const std::vector<StructuredField>& fields,
                 std::string_view key) {
  return std::ranges::any_of(
      fields, [key](const auto& field) { return field.first == key; });
}

}  // namespace

std::string encodeStructuredValue(std::string_view value) {
  if (!isValidText(value)) {
    throw ProtocolError("structured value is not valid text");
  }

  std::string encoded;
  encoded.reserve(value.size());
  for (const char character : value) {
    switch (character) {
      case '%':
        encoded += "%25";
        break;
      case '|':
        encoded += "%7C";
        break;
      case '=':
        encoded += "%3D";
        break;
      default:
        encoded.push_back(character);
        break;
    }
  }
  return encoded;
}

std::string decodeStructuredValue(std::string_view value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    const char character = value[index];
    if (character == '|' || character == '=') {
      throw ProtocolError("structured value contains an unescaped separator");
    }
    if (character != '%') {
      decoded.push_back(character);
      continue;
    }
    if (value.size() - index < 3) {
      throw ProtocolError("structured value has an incomplete escape");
    }
    const int high = hexValue(value[index + 1]);
    const int low = hexValue(value[index + 2]);
    if (high < 0 || low < 0) {
      throw ProtocolError("structured value has an invalid escape");
    }
    decoded.push_back(static_cast<char>((high << 4) | low));
    index += 2;
  }

  if (!isValidText(decoded)) {
    throw ProtocolError("decoded structured value is not valid text");
  }
  return decoded;
}

StructuredPayload StructuredPayload::parse(std::string_view payload) {
  if (payload.empty()) {
    throw ProtocolError("structured payload is empty");
  }

  StructuredPayload parsed;
  std::size_t position{};
  bool first = true;
  while (position < payload.size()) {
    const auto separator = payload.find('|', position);
    const auto end =
        separator == std::string_view::npos ? payload.size() : separator;
    const auto segment = payload.substr(position, end - position);
    if (segment.empty()) {
      throw ProtocolError("structured payload has an empty segment");
    }

    if (first && segment == "OK") {
      parsed.status_ = "OK";
    } else {
      const auto equals = segment.find('=');
      if (equals == std::string_view::npos || equals == 0 ||
          segment.find('=', equals + 1) != std::string_view::npos) {
        throw ProtocolError("structured payload has an invalid field");
      }
      const auto key = segment.substr(0, equals);
      if (!isValidKey(key) || containsKey(parsed.fields_, key)) {
        throw ProtocolError("structured payload has an invalid field key");
      }
      parsed.fields_.emplace_back(
          key, decodeStructuredValue(segment.substr(equals + 1)));
    }

    first = false;
    if (separator == std::string_view::npos) {
      break;
    }
    position = separator + 1;
    if (position == payload.size()) {
      throw ProtocolError("structured payload has a trailing separator");
    }
  }

  if (parsed.fields_.empty()) {
    throw ProtocolError("structured payload has no fields");
  }
  return parsed;
}

std::optional<std::string_view> StructuredPayload::status() const noexcept {
  if (!status_.has_value()) {
    return std::nullopt;
  }
  return *status_;
}

std::optional<std::string_view> StructuredPayload::field(
    std::string_view key) const noexcept {
  const auto found = std::ranges::find_if(
      fields_, [key](const auto& item) { return item.first == key; });
  if (found == fields_.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::string_view StructuredPayload::requireField(std::string_view key) const {
  const auto value = field(key);
  if (!value.has_value()) {
    throw ProtocolError("structured payload is missing a required field");
  }
  return *value;
}

const std::vector<StructuredField>& StructuredPayload::fields() const noexcept {
  return fields_;
}

StructuredPayloadBuilder::StructuredPayloadBuilder(std::string_view status) {
  if (status != "OK") {
    throw ProtocolError("structured payload status must be OK");
  }
  status_ = status;
}

StructuredPayloadBuilder& StructuredPayloadBuilder::addField(
    std::string_view key, std::string_view value) {
  if (!isValidKey(key) || containsKey(fields_, key)) {
    throw ProtocolError("structured payload has an invalid field key");
  }
  if (!isValidText(value)) {
    throw ProtocolError("structured value is not valid text");
  }
  fields_.emplace_back(key, value);
  return *this;
}

std::string StructuredPayloadBuilder::build() const {
  if (fields_.empty()) {
    throw ProtocolError("structured payload has no fields");
  }

  std::string payload;
  if (status_.has_value()) {
    payload = *status_;
  }
  for (const auto& [key, value] : fields_) {
    if (!payload.empty()) {
      payload.push_back('|');
    }
    payload.append(key);
    payload.push_back('=');
    payload.append(encodeStructuredValue(value));
  }
  return payload;
}

}  // namespace rss::protocol
