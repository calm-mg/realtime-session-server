#pragma once

#include <QByteArray>
#include <QTcpSocket>
#include <cstdint>
#include <string_view>

#include "rss/protocol/PacketCodec.h"
#include "rss/qt_client/application/SessionTransport.h"

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
  void failConnection(const QString& message);
  void resetBuffers();

  QTcpSocket socket_;
  protocol::PacketCodec codec_;
  QByteArray pending_bytes_;
  qsizetype pending_offset_{};
  bool fatal_error_reported_{};
};

}  // namespace rss::qt_client
