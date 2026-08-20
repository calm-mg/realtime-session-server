#pragma once

#include <QObject>
#include <QString>
#include <cstdint>
#include <string_view>

#include "rss/protocol/Packet.h"

namespace rss::qt_client {

enum class TransportErrorKind {
  Recoverable,
  Fatal,
};

class SessionTransport : public QObject {
  Q_OBJECT

 public:
  using QObject::QObject;
  ~SessionTransport() override = default;

  virtual void connectToHost(const QString& host, std::uint16_t port) = 0;
  virtual void disconnectFromHost() = 0;
  virtual bool sendPacket(protocol::PacketType type,
                          std::string_view payload) = 0;

 signals:
  void connected();
  void disconnected();
  void packetReceived(rss::protocol::Packet packet);
  void transportError(rss::qt_client::TransportErrorKind kind, QString message);
};

}  // namespace rss::qt_client

Q_DECLARE_METATYPE(rss::protocol::Packet)
Q_DECLARE_METATYPE(rss::qt_client::TransportErrorKind)
