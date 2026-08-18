#include "application/ClientController.h"

#include <QByteArray>
#include <QDateTime>
#include <optional>

namespace rss::qt_client {

namespace {

QString payloadText(const protocol::Packet& packet) {
  return QString::fromUtf8(reinterpret_cast<const char*>(packet.payload.data()),
                           static_cast<qsizetype>(packet.payload.size()));
}

bool isSuccessfulResponse(const QString& payload) {
  return payload == "OK" || payload.startsWith("OK|");
}

QString fieldValue(const QString& payload, const QString& key) {
  const QString marker = QString("|%1=").arg(key);
  const qsizetype marker_position = payload.indexOf(marker);
  if (marker_position < 0) {
    return {};
  }
  const qsizetype value_position = marker_position + marker.size();
  const qsizetype separator_position = payload.indexOf('|', value_position);
  return separator_position < 0
             ? payload.mid(value_position)
             : payload.mid(value_position, separator_position - value_position);
}

std::optional<qulonglong> sessionId(const QString& payload) {
  bool ok = false;
  const qulonglong value = fieldValue(payload, "session_id").toULongLong(&ok);
  return ok ? std::optional<qulonglong>{value} : std::nullopt;
}

ChatLogEntry logEntry(LogKind kind, const QString& text) {
  return {
      .kind = kind,
      .text = text,
      .received_at = QDateTime::currentDateTime(),
  };
}

ChatLogEntry chatEntry(const QString& payload,
                       std::optional<qulonglong> own_session_id) {
  const QString message_marker = "|message=";
  const qsizetype message_position = payload.indexOf(message_marker);
  const QString metadata =
      message_position < 0 ? payload : payload.left(message_position);
  const QString message =
      message_position < 0
          ? payload
          : payload.mid(message_position + message_marker.size());
  const auto sender_session_id = sessionId(metadata);
  return {
      .kind = LogKind::Chat,
      .author = fieldValue(metadata, "name"),
      .text = message,
      .received_at = QDateTime::currentDateTime(),
      .is_own = own_session_id.has_value() && sender_session_id.has_value() &&
                *own_session_id == *sender_session_id,
  };
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
  const QString value = username.trimmed();
  if (value.isEmpty()) {
    emit validationFailed("Enter a user name.");
    return;
  }
  sendTextPacket(protocol::PacketType::LoginReq, value);
}

void ClientController::createRoom(const QString& room_name) {
  if (!requireState(ClientState::LoggedIn, "Log in before creating a room.")) {
    return;
  }
  const QString value = room_name.trimmed();
  if (value.isEmpty()) {
    emit validationFailed("Enter a room name.");
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
  sendTextPacket(protocol::PacketType::JoinRoomReq, value);
}

void ClientController::leaveRoom() {
  if (!requireState(ClientState::InRoom, "Join a room before leaving it.")) {
    return;
  }
  sendTextPacket(protocol::PacketType::LeaveRoomReq, {});
}

void ClientController::sendChat(const QString& message) {
  if (!requireState(ClientState::InRoom,
                    "Join a room before sending a message.")) {
    return;
  }
  const QString value = message.trimmed();
  if (value.isEmpty()) {
    emit validationFailed("Enter a chat message.");
    return;
  }
  sendTextPacket(protocol::PacketType::ChatReq, value);
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
  const QString payload = payloadText(packet);
  const bool ok = isSuccessfulResponse(payload);

  switch (packet.type) {
    case protocol::PacketType::LoginRes:
      if (ok && state_ == ClientState::Connected) {
        session_id_ = sessionId(payload);
        setState(ClientState::LoggedIn);
      }
      break;
    case protocol::PacketType::CreateRoomRes:
    case protocol::PacketType::JoinRoomRes:
      if (ok && state_ == ClientState::LoggedIn) {
        setState(ClientState::InRoom);
      }
      break;
    case protocol::PacketType::LeaveRoomRes:
      if (ok && state_ == ClientState::InRoom) {
        setState(ClientState::LoggedIn);
      }
      break;
    case protocol::PacketType::RoomBroadcast:
      emit logEntryAdded(payload.startsWith("event=CHAT|")
                             ? chatEntry(payload, session_id_)
                             : logEntry(LogKind::System, payload));
      return;
    case protocol::PacketType::Error:
      emit logEntryAdded(logEntry(LogKind::Error, payload));
      return;
    default:
      return;
  }

  emit logEntryAdded(logEntry(ok ? LogKind::System : LogKind::Error, payload));
}

void ClientController::onTransportError(const QString& message) {
  emit logEntryAdded(logEntry(LogKind::Error, message));
  session_id_.reset();
  setState(ClientState::Disconnected);
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
                                      const QString& payload) {
  const QByteArray utf8 = payload.toUtf8();
  if (transport_.sendPacket(
          type, std::string_view(utf8.constData(),
                                 static_cast<std::size_t>(utf8.size())))) {
    return true;
  }
  emit logEntryAdded(
      logEntry(LogKind::Error, "The request could not be sent."));
  return false;
}

}  // namespace rss::qt_client
