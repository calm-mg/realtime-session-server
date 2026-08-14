#pragma once

#include <cstdint>
#include <vector>

#include "rss/protocol/Packet.h"

namespace rss::service {

enum class SessionEventKind {
  Packet,
  Disconnected,
};

struct SessionEvent {
  SessionEventKind kind{SessionEventKind::Packet};
  std::uint64_t session_id{};
  protocol::Packet packet;
  std::uint64_t sequence{};
};

struct OutboundMessage {
  std::uint64_t session_id{};
  std::vector<std::uint8_t> bytes;
};

}  // namespace rss::service
