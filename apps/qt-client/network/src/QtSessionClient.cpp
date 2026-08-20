#include "rss/qt_client/network/QtSessionClient.h"

#include <QAbstractSocket>
#include <QSignalBlocker>
#include <cstddef>
#include <utility>

namespace rss::qt_client {

QtSessionClient::QtSessionClient(QObject* parent) : SessionTransport(parent) {
  connect(&socket_, &QTcpSocket::connected, this, &SessionTransport::connected);
  connect(&socket_, &QTcpSocket::disconnected, this, [this] {
    resetBuffers();
    emit disconnected();
  });
  connect(&socket_, &QTcpSocket::readyRead, this,
          &QtSessionClient::readAvailableBytes);
  connect(&socket_, &QTcpSocket::bytesWritten, this,
          [this](qint64) { flushPendingWrites(); });
  connect(&socket_, &QTcpSocket::errorOccurred, this,
          [this](QAbstractSocket::SocketError error) {
            if (error == QAbstractSocket::RemoteHostClosedError) {
              return;
            }
            failConnection(socket_.errorString());
          });
}

QtSessionClient::~QtSessionClient() {
  disconnect(&socket_, nullptr, this, nullptr);
  socket_.abort();
}

void QtSessionClient::connectToHost(const QString& host, std::uint16_t port) {
  resetBuffers();
  fatal_error_reported_ = false;
  socket_.connectToHost(host, port);
}

void QtSessionClient::disconnectFromHost() { socket_.disconnectFromHost(); }

bool QtSessionClient::sendPacket(protocol::PacketType type,
                                 std::string_view payload) {
  if (socket_.state() != QAbstractSocket::ConnectedState) {
    return false;
  }

  try {
    const auto encoded = protocol::PacketCodec::encode(type, payload);
    pending_bytes_.append(reinterpret_cast<const char*>(encoded.data()),
                          static_cast<qsizetype>(encoded.size()));
  } catch (const protocol::ProtocolError& error) {
    emit transportError(TransportErrorKind::Recoverable,
                        QString::fromUtf8(error.what()));
    return false;
  }

  return flushPendingWrites();
}

void QtSessionClient::readAvailableBytes() {
  const QByteArray bytes = socket_.readAll();

  try {
    codec_.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                static_cast<std::size_t>(bytes.size()));
    for (auto& packet : codec_.drainPackets()) {
      emit packetReceived(std::move(packet));
    }
  } catch (const protocol::ProtocolError& error) {
    failConnection(
        QString("Protocol error: %1").arg(QString::fromUtf8(error.what())));
  }
}

bool QtSessionClient::flushPendingWrites() {
  if (pending_offset_ >= pending_bytes_.size()) {
    pending_bytes_.clear();
    pending_offset_ = 0;
    return true;
  }

  const auto remaining = pending_bytes_.size() - pending_offset_;
  const qint64 accepted =
      socket_.write(pending_bytes_.constData() + pending_offset_, remaining);
  if (accepted < 0) {
    failConnection(socket_.errorString());
    return false;
  }

  pending_offset_ += static_cast<qsizetype>(accepted);
  if (pending_offset_ == pending_bytes_.size()) {
    pending_bytes_.clear();
    pending_offset_ = 0;
  }
  return true;
}

void QtSessionClient::failConnection(const QString& message) {
  if (fatal_error_reported_) {
    return;
  }
  fatal_error_reported_ = true;
  {
    const QSignalBlocker blocker(&socket_);
    socket_.abort();
  }
  resetBuffers();
  emit transportError(TransportErrorKind::Fatal, message);
}

void QtSessionClient::resetBuffers() {
  codec_ = protocol::PacketCodec{};
  pending_bytes_.clear();
  pending_offset_ = 0;
}

}  // namespace rss::qt_client
