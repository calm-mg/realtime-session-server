#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "rss/protocol/Packet.h"

namespace rss::service {

enum class OutboundMessageKind {
  SendBytes,
  DisconnectSession,
};

struct OutboundMessage {
  std::uint64_t session_id{};
  std::vector<std::uint8_t> bytes;
  OutboundMessageKind kind{OutboundMessageKind::SendBytes};
};

struct DeferredCompletionPayload {
  bool failed{};
  std::vector<OutboundMessage> messages;
};

enum class SessionEventKind {
  Packet,
  Disconnected,
  DeferredCompletion,
};

struct SessionEvent {
  SessionEventKind kind{SessionEventKind::Packet};
  std::uint64_t session_id{};
  protocol::Packet packet;
  std::uint64_t sequence{};
  std::shared_ptr<DeferredCompletionPayload> completion;
};

}  // namespace rss::service
