#include "rss/service/MessageRouter.h"

#include <charconv>
#include <cstddef>
#include <exception>
#include <optional>
#include <sstream>
#include <system_error>

#include "rss/protocol/PacketCodec.h"
#include "rss/protocol/StructuredPayload.h"
#include "rss/protocol/TextValidation.h"

namespace rss::service {
namespace {

std::string payloadText(const protocol::Packet& packet) {
  return protocol::payloadToString(packet);
}

struct NormalizedLoginName {
  std::string normalized;
  std::string display;
};

std::optional<NormalizedLoginName> normalizeLoginName(std::string_view input) {
  const auto trimmed = protocol::trimAsciiWhitespace(input);
  if (trimmed.empty() || trimmed.size() > protocol::kMaxUserNameBytes ||
      !protocol::isValidText(trimmed)) {
    return std::nullopt;
  }

  NormalizedLoginName result;
  result.display.assign(trimmed);
  result.normalized = result.display;
  for (auto& character : result.normalized) {
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character - 'A' + 'a');
    }
  }
  return result;
}

std::optional<std::string> normalizeRoomName(std::string_view input) {
  const auto trimmed = protocol::trimAsciiWhitespace(input);
  if (trimmed.empty() || trimmed.size() > protocol::kMaxRoomNameBytes ||
      !protocol::isValidText(trimmed)) {
    return std::nullopt;
  }
  return std::string(trimmed);
}

std::string_view persistenceErrorText(persistence::PersistenceErrorKind kind) {
  switch (kind) {
    case persistence::PersistenceErrorKind::Busy:
      return "user persistence busy";
    case persistence::PersistenceErrorKind::Unavailable:
      return "user persistence unavailable";
    case persistence::PersistenceErrorKind::Timeout:
      return "user persistence timeout";
    case persistence::PersistenceErrorKind::Stopping:
      return "server is stopping";
    case persistence::PersistenceErrorKind::Constraint:
    case persistence::PersistenceErrorKind::InvalidData:
      return "user persistence failure";
  }
  return "user persistence failure";
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

void addUserFields(protocol::StructuredPayloadBuilder& builder,
                   const domain::User& user) {
  builder.addField("user_id", user.id.toString())
      .addField("session_id", std::to_string(user.session_id))
      .addField("name", user.name);
}

std::string roomPayload(bool include_ok_status, std::string_view event,
                        const RoomActionResult& result) {
  auto builder = include_ok_status ? protocol::StructuredPayloadBuilder("OK")
                                   : protocol::StructuredPayloadBuilder();
  builder.addField("event", event);
  if (result.room_id != 0) {
    builder.addField("room_id", std::to_string(result.room_id));
  }
  addUserFields(builder, result.actor);
  return builder.build();
}

}  // namespace

MessageRouter::MessageRouter(RoomService& room_service,
                             persistence::UserRepository& user_repository)
    : room_service_(room_service), user_repository_(user_repository) {}

void MessageRouter::handle(const SessionEvent& event,
                           SessionEventContext& context) {
  if (event.kind == SessionEventKind::Disconnected) {
    const auto result = room_service_.disconnect(event.session_id);
    if (!result.ok || result.room_id == 0) {
      return;
    }

    const auto payload = roomPayload(true, "LEAVE", result);
    for (const auto recipient : result.recipients) {
      if (recipient != event.session_id &&
          !context.emit(
              make(recipient, protocol::PacketType::RoomBroadcast, payload))) {
        return;
      }
    }
    return;
  }

  try {
    handlePacket(event.session_id, event.packet, context);
  } catch (const std::exception& ex) {
    static_cast<void>(context.emit(error(event.session_id, ex.what())));
  }
}

void MessageRouter::handlePacket(std::uint64_t session_id,
                                 const protocol::Packet& packet,
                                 SessionEventContext& context) {
  auto& sink = static_cast<OutboundMessageSink&>(context);
  using protocol::PacketType;

  switch (packet.type) {
    case PacketType::LoginReq: {
      const auto normalized = normalizeLoginName(payloadText(packet));
      if (!normalized.has_value()) {
        static_cast<void>(sink.emit(error(session_id, "invalid user name")));
        return;
      }
      auto completion = context.defer();
      try {
        user_repository_.findOrCreateByNormalizedName(
            {normalized->normalized, normalized->display},
            [this, session_id, completion](persistence::UserResult result) {
              try {
                if (!result.user.has_value()) {
                  const auto message =
                      result.error.has_value()
                          ? persistenceErrorText(result.error->kind)
                          : std::string_view{"user persistence failure"};
                  static_cast<void>(
                      completion->succeed({error(session_id, message)}));
                  return;
                }
                const auto login =
                    room_service_.attachUser(session_id, *result.user);
                if (!login.ok) {
                  static_cast<void>(
                      completion->succeed({error(session_id, login.error)}));
                  return;
                }
                protocol::StructuredPayloadBuilder payload("OK");
                addUserFields(payload, login.user);
                static_cast<void>(completion->succeed(
                    {make(session_id, PacketType::LoginRes, payload.build())}));
              } catch (...) {
                static_cast<void>(completion->fail());
              }
            });
      } catch (...) {
        static_cast<void>(completion->fail());
      }
      return;
    }
    case PacketType::CreateRoomReq: {
      const auto room_name = normalizeRoomName(payloadText(packet));
      if (!room_name.has_value()) {
        static_cast<void>(sink.emit(error(session_id, "invalid room name")));
        return;
      }
      const auto result = room_service_.createRoom(session_id, *room_name);
      if (!result.ok) {
        static_cast<void>(sink.emit(error(session_id, result.error)));
        return;
      }
      static_cast<void>(
          sink.emit(make(session_id, PacketType::CreateRoomRes,
                         roomPayload(true, "CREATE_ROOM", result))));
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
                          roomPayload(true, "JOIN_ROOM", result)))) {
        return;
      }
      const auto broadcast = roomPayload(true, "JOIN", result);
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
                          roomPayload(true, "LEAVE_ROOM", result)))) {
        return;
      }
      const auto broadcast = roomPayload(true, "LEAVE", result);
      for (const auto recipient : result.recipients) {
        if (recipient != session_id &&
            !sink.emit(make(recipient, PacketType::RoomBroadcast, broadcast))) {
          return;
        }
      }
      return;
    }
    case PacketType::ChatReq: {
      const auto message = payloadText(packet);
      if (message.size() > protocol::kMaxChatMessageBytes) {
        static_cast<void>(
            sink.emit(error(session_id, "chat message too large")));
        return;
      }
      if (!protocol::isValidText(message)) {
        static_cast<void>(sink.emit(error(session_id, "invalid chat message")));
        return;
      }
      const auto result = room_service_.chat(session_id);
      if (!result.ok) {
        static_cast<void>(sink.emit(error(session_id, result.error)));
        return;
      }
      protocol::StructuredPayloadBuilder payload;
      payload.addField("event", "CHAT")
          .addField("room_id", std::to_string(result.room_id));
      addUserFields(payload, result.actor);
      payload.addField("message", message);
      const auto encoded_payload = payload.build();

      for (const auto recipient : result.recipients) {
        if (!sink.emit(
                make(recipient, PacketType::RoomBroadcast, encoded_payload))) {
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
      std::ostringstream x_text;
      std::ostringstream y_text;
      x_text << x;
      y_text << y;
      protocol::StructuredPayloadBuilder payload;
      payload.addField("event", "POSITION")
          .addField("room_id", std::to_string(result.room_id));
      addUserFields(payload, result.actor);
      payload.addField("x", x_text.str()).addField("y", y_text.str());
      const auto encoded_payload = payload.build();

      for (const auto recipient : result.recipients) {
        if (!sink.emit(
                make(recipient, PacketType::RoomBroadcast, encoded_payload))) {
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
