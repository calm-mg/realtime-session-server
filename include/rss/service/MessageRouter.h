#pragma once

#include "rss/service/Command.h"
#include "rss/service/RoomService.h"

#include <string>
#include <string_view>
#include <vector>

namespace rss::service {

class MessageRouter {
public:
    explicit MessageRouter(RoomService& room_service);

    [[nodiscard]] std::vector<OutboundMessage> handle(const SessionEvent& event);

private:
    [[nodiscard]] std::vector<OutboundMessage> handlePacket(std::uint64_t session_id, const protocol::Packet& packet);
    [[nodiscard]] OutboundMessage make(std::uint64_t session_id, protocol::PacketType type, std::string_view payload) const;
    [[nodiscard]] OutboundMessage error(std::uint64_t session_id, std::string_view message) const;

    RoomService& room_service_;
};

} // namespace rss::service
