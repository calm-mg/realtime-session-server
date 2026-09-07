#pragma once

#include <stdexcept>
#include <string>

namespace rss::net {

enum class ClientIoFailure { PeerClosed, SocketError, Timeout };

class ClientIoError final : public std::runtime_error {
 public:
  ClientIoError(ClientIoFailure cause, const std::string& message)
      : std::runtime_error(message), cause_(cause) {}

  [[nodiscard]] ClientIoFailure cause() const noexcept { return cause_; }

 private:
  ClientIoFailure cause_;
};

}  // namespace rss::net
