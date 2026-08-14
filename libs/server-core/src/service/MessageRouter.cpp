#include "rss/service/MessageRouter.h"

#include <charconv>
#include <exception>
#include <sstream>
#include <system_error>

#include "rss/protocol/PacketCodec.h"

namespace rss::service {
namespace {

std::string payloadText(const protocol::Packet& packet) {
  return protocol::payloadToString(packet);
}

std::uint32_t parseRoomId(const std::string& text) {
  std::uint32_t room_id{};
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, room_id);
  if (ec != std::errc{} || ptr != end) {
    throw protocol::ProtocolError("invalid room id");
  }
  return room_id;
}

std::string userPrefix(const domain::User& user) {
  std::ostringstream out;
  out << "user_id=" << user.id << "|session_id=" << user.session_id
      << "|name=" << user.name;
  return out.str();
}

std::string okPayload(std::string_view event, const RoomActionResult& result) {
  std::ostringstream out;
  out << "OK|event=" << event;
  if (result.room_id != 0) {
    out << "|room_id=" << result.room_id;
  }
  out << "|" << userPrefix(result.actor);
  return out.str();
}

}  // namespace

MessageRouter::MessageRouter(RoomService& room_service)
    : room_service_(room_service) {}

void MessageRouter::handle(const SessionEvent& event,
                           OutboundMessageSink& sink) {
  if (event.kind == SessionEventKind::Disconnected) {
    const auto result = room_service_.disconnect(event.session_id);
    if (!result.ok || result.room_id == 0) {
      return;
    }

    const auto payload = okPayload("LEAVE", result);
    for (const auto recipient : result.recipients) {
      if (recipient != event.session_id &&
          !sink.emit(
              make(recipient, protocol::PacketType::RoomBroadcast, payload))) {
        return;
      }
    }
    return;
  }

  try {
    handlePacket(event.session_id, event.packet, sink);
  } catch (const std::exception& ex) {
    static_cast<void>(sink.emit(error(event.session_id, ex.what())));
  }
}

void MessageRouter::handlePacket(std::uint64_t session_id,
                                 const protocol::Packet& packet,
                                 OutboundMessageSink& sink) {
  using protocol::PacketType;

  switch (packet.type) {
    case PacketType::LoginReq: {
      const auto user = room_service_.login(session_id, payloadText(packet));
      std::ostringstream payload;
      payload << "OK|" << userPrefix(user);
      static_cast<void>(
          sink.emit(make(session_id, PacketType::LoginRes, payload.str())));
      return;
    }
    case PacketType::CreateRoomReq: {
      const auto result =
          room_service_.createRoom(session_id, payloadText(packet));
      if (!result.ok) {
        static_cast<void>(sink.emit(error(session_id, result.error)));
        return;
      }
      static_cast<void>(sink.emit(make(session_id, PacketType::CreateRoomRes,
                                       okPayload("CREATE_ROOM", result))));
      return;
    }
    case PacketType::JoinRoomReq: {
      const auto result =
          room_service_.joinRoom(session_id, parseRoomId(payloadText(packet)));
      if (!result.ok) {
        static_cast<void>(sink.emit(error(session_id, result.error)));
        return;
      }

      if (!sink.emit(make(session_id, PacketType::JoinRoomRes,
                          okPayload("JOIN_ROOM", result)))) {
        return;
      }
      const auto broadcast = okPayload("JOIN", result);
      for (const auto recipient : result.recipients) {
        if (recipient != session_id &&
            !sink.emit(make(recipient, PacketType::RoomBroadcast, broadcast))) {
          return;
        }
      }
      return;
    }
    case PacketType::LeaveRoomReq: {
      const auto result = room_service_.leaveRoom(session_id);
      if (!result.ok) {
        static_cast<void>(sink.emit(error(session_id, result.error)));
        return;
      }

      if (!sink.emit(make(session_id, PacketType::LeaveRoomRes,
                          okPayload("LEAVE_ROOM", result)))) {
        return;
      }
      const auto broadcast = okPayload("LEAVE", result);
      for (const auto recipient : result.recipients) {
        if (recipient != session_id &&
            !sink.emit(make(recipient, PacketType::RoomBroadcast, broadcast))) {
          return;
        }
      }
      return;
    }
    case PacketType::ChatReq: {
      const auto result = room_service_.chat(session_id);
      if (!result.ok) {
        static_cast<void>(sink.emit(error(session_id, result.error)));
        return;
      }
      std::ostringstream payload;
      payload << "event=CHAT|room_id=" << result.room_id << "|"
              << userPrefix(result.actor) << "|message=" << payloadText(packet);

      for (const auto recipient : result.recipients) {
        if (!sink.emit(
                make(recipient, PacketType::RoomBroadcast, payload.str()))) {
          return;
        }
      }
      return;
    }
    case PacketType::PositionUpdate: {
      const auto [x, y] = protocol::PacketCodec::decodePosition(packet.payload);
      const auto result =
          room_service_.updatePosition(session_id, domain::Position{x, y});
      if (!result.ok) {
        static_cast<void>(sink.emit(error(session_id, result.error)));
        return;
      }
      std::ostringstream payload;
      payload << "event=POSITION|room_id=" << result.room_id << "|"
              << userPrefix(result.actor) << "|x=" << x << "|y=" << y;

      for (const auto recipient : result.recipients) {
        if (!sink.emit(
                make(recipient, PacketType::RoomBroadcast, payload.str()))) {
          return;
        }
      }
      return;
    }
    case PacketType::Ping:
      static_cast<void>(sink.emit(make(session_id, PacketType::Pong, "PONG")));
      return;
    default:
      static_cast<void>(
          sink.emit(error(session_id, "unsupported packet type")));
      return;
  }
}

OutboundMessage MessageRouter::make(std::uint64_t session_id,
                                    protocol::PacketType type,
                                    std::string_view payload) const {
  return OutboundMessage{session_id,
                         protocol::PacketCodec::encode(type, payload)};
}

OutboundMessage MessageRouter::error(std::uint64_t session_id,
                                     std::string_view message) const {
  return make(session_id, protocol::PacketType::Error, message);
}

}  // namespace rss::service
