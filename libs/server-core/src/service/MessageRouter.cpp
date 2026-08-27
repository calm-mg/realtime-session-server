#include "rss/service/MessageRouter.h"

#include <charconv>
#include <cstddef>
#include <exception>
#include <optional>
#include <sstream>
#include <system_error>

#include "rss/protocol/PacketCodec.h"

namespace rss::service {
namespace {

std::string payloadText(const protocol::Packet& packet) {
  return protocol::payloadToString(packet);
}

struct NormalizedLoginName {
  std::string normalized;
  std::string display;
};

bool isAsciiSpace(char character) {
  return character == ' ' || character == '\t' || character == '\n' ||
         character == '\r' || character == '\f' || character == '\v';
}

std::optional<NormalizedLoginName> normalizeLoginName(std::string_view input) {
  std::size_t begin{};
  while (begin < input.size() && isAsciiSpace(input[begin])) {
    ++begin;
  }
  std::size_t end = input.size();
  while (end > begin && isAsciiSpace(input[end - 1])) {
    --end;
  }
  if (begin == end || end - begin > 32) {
    return std::nullopt;
  }

  NormalizedLoginName result;
  result.display.assign(input.substr(begin, end - begin));
  result.normalized = result.display;
  for (auto& character : result.normalized) {
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character - 'A' + 'a');
    }
  }
  return result;
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

std::string userPrefix(const domain::User& user) {
  std::ostringstream out;
  out << "user_id=" << user.id.toString() << "|session_id=" << user.session_id
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

    const auto payload = okPayload("LEAVE", result);
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
                std::ostringstream payload;
                payload << "OK|" << userPrefix(login.user);
                static_cast<void>(completion->succeed(
                    {make(session_id, PacketType::LoginRes, payload.str())}));
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
