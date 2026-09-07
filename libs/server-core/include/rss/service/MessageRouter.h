#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "rss/persistence/UserRepository.h"
#include "rss/service/Command.h"
#include "rss/service/RoomService.h"
#include "rss/service/SessionEventHandler.h"

namespace rss::service {

class MessageRouter final : public SessionEventHandler {
 public:
  MessageRouter(RoomService& room_service,
                persistence::UserRepository& user_repository);

  void handle(const SessionEvent& event, SessionEventContext& context) override;

 private:
  bool handleNegotiation(std::uint64_t session_id,
                         const protocol::Packet& packet,
                         SessionEventContext& context);
  void handlePacket(std::uint64_t session_id, const protocol::Packet& packet,
                    SessionEventContext& context);
  [[nodiscard]] OutboundMessage make(std::uint64_t session_id,
                                     protocol::PacketType type,
                                     std::string_view payload) const;
  [[nodiscard]] OutboundMessage error(std::uint64_t session_id,
                                      std::string_view message) const;

  std::mutex negotiation_mutex_;
  std::unordered_map<std::uint64_t, std::uint16_t> negotiated_versions_;
  RoomService& room_service_;
  persistence::UserRepository& user_repository_;
};

}  // namespace rss::service
