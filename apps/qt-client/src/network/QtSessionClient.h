#pragma once

#include <QByteArray>
#include <QTcpSocket>
#include <cstdint>
#include <string_view>

#include "network/SessionTransport.h"
#include "rss/protocol/PacketCodec.h"

namespace rss::qt_client {

class QtSessionClient final : public SessionTransport {
  Q_OBJECT

 public:
  explicit QtSessionClient(QObject* parent = nullptr);
  ~QtSessionClient() override;

  void connectToHost(const QString& host, std::uint16_t port) override;
  void disconnectFromHost() override;
  bool sendPacket(protocol::PacketType type, std::string_view payload) override;

 private:
  void readAvailableBytes();
  bool flushPendingWrites();
  void resetBuffers();

  QTcpSocket socket_;
  protocol::PacketCodec codec_;
  QByteArray pending_bytes_;
  qsizetype pending_offset_{};
};

}  // namespace rss::qt_client
