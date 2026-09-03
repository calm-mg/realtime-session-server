#pragma once

#include <stdexcept>
#include <string>

namespace rss::protocol {

class ProtocolError final : public std::runtime_error {
 public:
  explicit ProtocolError(const char* message);
  explicit ProtocolError(const std::string& message);
};

}  // namespace rss::protocol
