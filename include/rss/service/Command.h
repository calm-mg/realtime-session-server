#pragma once

#include "rss/protocol/Packet.h"

#include <cstdint>
#include <vector>

namespace rss::service {

enum class SessionEventKind {
    Packet,
    Disconnected,
};

struct SessionEvent {
    SessionEventKind kind{SessionEventKind::Packet};
    std::uint64_t session_id{};
    protocol::Packet packet;
};

struct OutboundMessage {
    std::uint64_t session_id{};
    std::vector<std::uint8_t> bytes;
};

} // namespace rss::service
