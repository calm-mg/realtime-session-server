#pragma once

#include <QObject>
#include <QString>
#include <cstdint>
#include <optional>

#include "application/ChatLogEntry.h"
#include "application/ClientState.h"
#include "network/SessionTransport.h"

namespace rss::qt_client {

class ClientController final : public QObject {
  Q_OBJECT

 public:
  explicit ClientController(SessionTransport& transport,
                            QObject* parent = nullptr);

  [[nodiscard]] ClientState state() const noexcept;

  void connectToServer(const QString& host, std::uint16_t port);
  void disconnectFromServer();
  void login(const QString& username);
  void createRoom(const QString& room_name);
  void joinRoom(const QString& room_id);
  void leaveRoom();
  void sendChat(const QString& message);

 signals:
  void stateChanged(rss::qt_client::ClientState state);
  void logEntryAdded(rss::qt_client::ChatLogEntry entry);
  void validationFailed(QString message);

 private:
  void onConnected();
  void onDisconnected();
  void onPacketReceived(const protocol::Packet& packet);
  void onTransportError(const QString& message);
  void setState(ClientState state);
  bool requireState(ClientState expected, const QString& message);
  bool sendTextPacket(protocol::PacketType type, const QString& payload);

  SessionTransport& transport_;
  ClientState state_{ClientState::Disconnected};
  std::optional<qulonglong> session_id_;
};

}  // namespace rss::qt_client
