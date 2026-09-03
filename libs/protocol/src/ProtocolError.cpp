#include "rss/protocol/ProtocolError.h"

namespace rss::protocol {

ProtocolError::ProtocolError(const char* message)
    : std::runtime_error(message) {}

ProtocolError::ProtocolError(const std::string& message)
    : std::runtime_error(message) {}

}  // namespace rss::protocol
