#pragma once

#include <QObject>
#include <QString>
#include <cstdint>
#include <optional>
#include <string_view>

#include "rss/qt_client/application/ChatLogEntry.h"
#include "rss/qt_client/application/ClientState.h"
#include "rss/qt_client/application/SessionTransport.h"

namespace rss::qt_client {

class ClientController final : public QObject {
  Q_OBJECT

 public:
  explicit ClientController(SessionTransport& transport,
                            QObject* parent = nullptr);

  [[nodiscard]] ClientState state() const noexcept;
  [[nodiscard]] PendingRequest pendingRequest() const noexcept;

  void connectToServer(const QString& host, std::uint16_t port);
  void disconnectFromServer();
  void login(const QString& username);
  void createRoom(const QString& room_name);
  void joinRoom(const QString& room_id);
  void leaveRoom();
  bool sendChat(const QString& message);

 signals:
  void stateChanged(rss::qt_client::ClientState state);
  void pendingRequestChanged(rss::qt_client::PendingRequest request);
  void logEntryAdded(rss::qt_client::ChatLogEntry entry);
  void validationFailed(QString message);

 private:
  void onConnected();
  void onDisconnected();
  void onPacketReceived(const protocol::Packet& packet);
  void onTransportError(TransportErrorKind kind, const QString& message);
  void setState(ClientState state);
  void setPendingRequest(PendingRequest request);
  bool requireState(ClientState expected, const QString& message);
  bool requireNoPendingRequest();
  bool sendTextPacket(protocol::PacketType type, std::string_view payload);
  bool sendRequest(protocol::PacketType type, std::string_view payload,
                   PendingRequest request);
  [[nodiscard]] bool isExpectedResponse(
      protocol::PacketType type) const noexcept;

  SessionTransport& transport_;
  ClientState state_{ClientState::Disconnected};
  PendingRequest pending_request_{PendingRequest::None};
  std::optional<qulonglong> session_id_;
};

}  // namespace rss::qt_client
