#pragma once

#include <string>
#include <string_view>

#include "rss/service/Command.h"
#include "rss/service/RoomService.h"
#include "rss/service/SessionEventHandler.h"

namespace rss::service {

class MessageRouter final : public SessionEventHandler {
 public:
  explicit MessageRouter(RoomService& room_service);

  void handle(const SessionEvent& event, OutboundMessageSink& sink) override;

 private:
  void handlePacket(std::uint64_t session_id, const protocol::Packet& packet,
                    OutboundMessageSink& sink);
  [[nodiscard]] OutboundMessage make(std::uint64_t session_id,
                                     protocol::PacketType type,
                                     std::string_view payload) const;
  [[nodiscard]] OutboundMessage error(std::uint64_t session_id,
                                      std::string_view message) const;

  RoomService& room_service_;
};

}  // namespace rss::service
