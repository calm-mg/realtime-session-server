#include "rss/qt_client/application/ClientController.h"

#include <QByteArray>
#include <QDateTime>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "rss/protocol/PacketCodec.h"
#include "rss/protocol/ProtocolError.h"
#include "rss/protocol/StructuredPayload.h"
#include "rss/protocol/TextValidation.h"

namespace rss::qt_client {

namespace {

constexpr auto kProtocolErrorText =
    "Protocol error: invalid structured payload.";

QString toQString(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::uint64_t positiveInteger(std::string_view text) {
  std::uint64_t value{};
  const auto [position, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || error != std::errc{} ||
      position != text.data() + text.size() || value == 0) {
    throw protocol::ProtocolError("invalid positive integer field");
  }
  return value;
}

void requireStatus(const protocol::StructuredPayload& payload,
                   std::optional<std::string_view> expected) {
  if (payload.status() != expected) {
    throw protocol::ProtocolError("unexpected structured payload status");
  }
}

void requireNonEmpty(const protocol::StructuredPayload& payload,
                     std::string_view key) {
  if (payload.requireField(key).empty()) {
    throw protocol::ProtocolError("required field is empty");
  }
}

std::uint64_t requireUserFields(const protocol::StructuredPayload& payload) {
  requireNonEmpty(payload, "user_id");
  requireNonEmpty(payload, "name");
  return positiveInteger(payload.requireField("session_id"));
}

void requireRoomFields(const protocol::StructuredPayload& payload,
                       std::string_view event) {
  requireStatus(payload, std::string_view{"OK"});
  if (payload.requireField("event") != event) {
    throw protocol::ProtocolError("unexpected room response event");
  }
  static_cast<void>(positiveInteger(payload.requireField("room_id")));
  static_cast<void>(requireUserFields(payload));
}

QString displayText(const protocol::StructuredPayload& payload) {
  QString text;
  bool needs_separator = false;
  if (const auto status = payload.status(); status.has_value()) {
    text = toQString(*status);
    needs_separator = true;
  }
  for (const auto& [key, value] : payload.fields()) {
    if (needs_separator) {
      text += '|';
    }
    text += toQString(key);
    text += '=';
    text += toQString(value);
    needs_separator = true;
  }
  return text;
}

ChatLogEntry logEntry(LogKind kind, const QString& text) {
  return {
      .kind = kind,
      .text = text,
      .received_at = QDateTime::currentDateTime(),
  };
}

ChatLogEntry chatEntry(const protocol::StructuredPayload& payload,
                       std::optional<qulonglong> own_session_id) {
  const auto sender_session_id = requireUserFields(payload);
  static_cast<void>(positiveInteger(payload.requireField("room_id")));
  return {
      .kind = LogKind::Chat,
      .author = toQString(payload.requireField("name")),
      .text = toQString(payload.requireField("message")),
      .received_at = QDateTime::currentDateTime(),
      .is_own =
          own_session_id.has_value() && *own_session_id == sender_session_id,
  };
}

void validateBroadcast(const protocol::StructuredPayload& payload) {
  const auto event = payload.requireField("event");
  if (event == "JOIN" || event == "LEAVE") {
    requireStatus(payload, std::string_view{"OK"});
  } else if (event == "CHAT") {
    requireStatus(payload, std::nullopt);
    static_cast<void>(payload.requireField("message"));
  } else if (event == "POSITION") {
    requireStatus(payload, std::nullopt);
    requireNonEmpty(payload, "x");
    requireNonEmpty(payload, "y");
  } else {
    throw protocol::ProtocolError("unknown room broadcast event");
  }
  static_cast<void>(positiveInteger(payload.requireField("room_id")));
  static_cast<void>(requireUserFields(payload));
}

}  // namespace

ClientController::ClientController(SessionTransport& transport, QObject* parent)
    : QObject(parent), transport_(transport) {
  connect(&transport_, &SessionTransport::connected, this,
          &ClientController::onConnected);
  connect(&transport_, &SessionTransport::disconnected, this,
          &ClientController::onDisconnected);
  connect(&transport_, &SessionTransport::packetReceived, this,
          &ClientController::onPacketReceived);
  connect(&transport_, &SessionTransport::transportError, this,
          &ClientController::onTransportError);
}

ClientState ClientController::state() const noexcept { return state_; }

void ClientController::connectToServer(const QString& host,
                                       std::uint16_t port) {
  if (!requireState(ClientState::Disconnected,
                    "Disconnect before starting another connection.")) {
    return;
  }
  if (host.trimmed().isEmpty()) {
    emit validationFailed("Enter a server address.");
    return;
  }
  if (port == 0) {
    emit validationFailed("Enter a port between 1 and 65535.");
    return;
  }

  setState(ClientState::Connecting);
  transport_.connectToHost(host.trimmed(), port);
}

void ClientController::disconnectFromServer() {
  if (state_ == ClientState::Disconnected) {
    emit validationFailed("The client is already disconnected.");
    return;
  }
  transport_.disconnectFromHost();
}

void ClientController::login(const QString& username) {
  if (!requireState(ClientState::Connected,
                    "Connect to the server before logging in.")) {
    return;
  }
  const QByteArray utf8 = username.toUtf8();
  const auto value = protocol::trimAsciiWhitespace(std::string_view(
      utf8.constData(), static_cast<std::size_t>(utf8.size())));
  if (value.empty() || value.size() > protocol::kMaxUserNameBytes ||
      !protocol::isValidText(value)) {
    emit validationFailed("Enter a valid user name of at most 32 UTF-8 bytes.");
    return;
  }
  sendTextPacket(protocol::PacketType::LoginReq, value);
}

void ClientController::createRoom(const QString& room_name) {
  if (!requireState(ClientState::LoggedIn, "Log in before creating a room.")) {
    return;
  }
  const QByteArray utf8 = room_name.toUtf8();
  const auto value = protocol::trimAsciiWhitespace(std::string_view(
      utf8.constData(), static_cast<std::size_t>(utf8.size())));
  if (value.empty() || value.size() > protocol::kMaxRoomNameBytes ||
      !protocol::isValidText(value)) {
    emit validationFailed("Enter a valid room name of at most 32 UTF-8 bytes.");
    return;
  }
  sendTextPacket(protocol::PacketType::CreateRoomReq, value);
}

void ClientController::joinRoom(const QString& room_id) {
  if (!requireState(ClientState::LoggedIn, "Log in before joining a room.")) {
    return;
  }
  const QString value = room_id.trimmed();
  bool numeric = false;
  const qulonglong parsed = value.toULongLong(&numeric, 10);
  if (!numeric || parsed == 0) {
    emit validationFailed("Enter a positive numeric room ID.");
    return;
  }
  const QByteArray utf8 = value.toUtf8();
  sendTextPacket(protocol::PacketType::JoinRoomReq,
                 std::string_view(utf8.constData(),
                                  static_cast<std::size_t>(utf8.size())));
}

void ClientController::leaveRoom() {
  if (!requireState(ClientState::InRoom, "Join a room before leaving it.")) {
    return;
  }
  sendTextPacket(protocol::PacketType::LeaveRoomReq, std::string_view{});
}

bool ClientController::sendChat(const QString& message) {
  if (!requireState(ClientState::InRoom,
                    "Join a room before sending a message.")) {
    return false;
  }
  const QByteArray utf8 = message.toUtf8();
  const std::string_view value(utf8.constData(),
                               static_cast<std::size_t>(utf8.size()));
  if (value.size() > protocol::kMaxChatMessageBytes) {
    emit validationFailed("Chat messages are limited to 1291 UTF-8 bytes.");
    return false;
  }
  if (!protocol::isValidText(value)) {
    emit validationFailed("Enter a valid chat message.");
    return false;
  }
  return sendTextPacket(protocol::PacketType::ChatReq, value);
}

void ClientController::onConnected() {
  if (state_ != ClientState::Connecting) {
    return;
  }
  setState(ClientState::Connected);
  emit logEntryAdded(logEntry(LogKind::System, "Connected to the server."));
}

void ClientController::onDisconnected() {
  setState(ClientState::Disconnected);
  session_id_.reset();
  emit logEntryAdded(
      logEntry(LogKind::System, "Disconnected from the server."));
}

void ClientController::onPacketReceived(const protocol::Packet& packet) {
  const auto raw_payload = protocol::payloadToString(packet);

  try {
    switch (packet.type) {
      case protocol::PacketType::LoginRes: {
        const auto payload = protocol::StructuredPayload::parse(raw_payload);
        requireStatus(payload, std::string_view{"OK"});
        const auto parsed_session_id = requireUserFields(payload);
        if (state_ == ClientState::Connected) {
          session_id_ = parsed_session_id;
          setState(ClientState::LoggedIn);
        }
        emit logEntryAdded(logEntry(LogKind::System, displayText(payload)));
        return;
      }
      case protocol::PacketType::CreateRoomRes: {
        const auto payload = protocol::StructuredPayload::parse(raw_payload);
        requireRoomFields(payload, "CREATE_ROOM");
        if (state_ == ClientState::LoggedIn) {
          setState(ClientState::InRoom);
        }
        emit logEntryAdded(logEntry(LogKind::System, displayText(payload)));
        return;
      }
      case protocol::PacketType::JoinRoomRes: {
        const auto payload = protocol::StructuredPayload::parse(raw_payload);
        requireRoomFields(payload, "JOIN_ROOM");
        if (state_ == ClientState::LoggedIn) {
          setState(ClientState::InRoom);
        }
        emit logEntryAdded(logEntry(LogKind::System, displayText(payload)));
        return;
      }
      case protocol::PacketType::LeaveRoomRes: {
        const auto payload = protocol::StructuredPayload::parse(raw_payload);
        requireRoomFields(payload, "LEAVE_ROOM");
        if (state_ == ClientState::InRoom) {
          setState(ClientState::LoggedIn);
        }
        emit logEntryAdded(logEntry(LogKind::System, displayText(payload)));
        return;
      }
      case protocol::PacketType::RoomBroadcast: {
        const auto payload = protocol::StructuredPayload::parse(raw_payload);
        validateBroadcast(payload);
        emit logEntryAdded(
            payload.requireField("event") == "CHAT"
                ? chatEntry(payload, session_id_)
                : logEntry(LogKind::System, displayText(payload)));
        return;
      }
      case protocol::PacketType::Error:
        if (!protocol::isValidText(raw_payload)) {
          throw protocol::ProtocolError("invalid error payload text");
        }
        emit logEntryAdded(logEntry(LogKind::Error, toQString(raw_payload)));
        return;
      default:
        return;
    }
  } catch (const protocol::ProtocolError&) {
    emit logEntryAdded(logEntry(LogKind::Error, kProtocolErrorText));
  }
}

void ClientController::onTransportError(TransportErrorKind kind,
                                        const QString& message) {
  emit logEntryAdded(logEntry(LogKind::Error, message));
  if (kind == TransportErrorKind::Fatal) {
    session_id_.reset();
    setState(ClientState::Disconnected);
  }
}

void ClientController::setState(ClientState state) {
  if (state_ == state) {
    return;
  }
  state_ = state;
  emit stateChanged(state_);
}

bool ClientController::requireState(ClientState expected,
                                    const QString& message) {
  if (state_ == expected) {
    return true;
  }
  emit validationFailed(message);
  return false;
}

bool ClientController::sendTextPacket(protocol::PacketType type,
                                      std::string_view payload) {
  if (transport_.sendPacket(type, payload)) {
    return true;
  }
  emit logEntryAdded(
      logEntry(LogKind::Error, "The request could not be sent."));
  return false;
}

}  // namespace rss::qt_client
