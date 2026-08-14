#pragma once

#include <string>
#include <utility>

#include "network/SessionTransport.h"

class FakeSessionTransport final : public rss::qt_client::SessionTransport {
 public:
  void connectToHost(const QString& host, std::uint16_t port) override {
    host_ = host;
    port_ = port;
  }

  void disconnectFromHost() override { emit disconnected(); }

  bool sendPacket(rss::protocol::PacketType type,
                  std::string_view payload) override {
    last_type_ = type;
    last_payload_.assign(payload);
    ++sent_count_;
    return send_succeeds_;
  }

  void completeConnection() { emit connected(); }
  void completeDisconnection() { emit disconnected(); }
  void fail(const QString& message) { emit transportError(message); }
  void receive(rss::protocol::Packet packet) {
    emit packetReceived(std::move(packet));
  }

  [[nodiscard]] rss::protocol::PacketType lastType() const {
    return last_type_;
  }
  [[nodiscard]] const std::string& lastPayload() const { return last_payload_; }
  [[nodiscard]] int sentCount() const { return sent_count_; }

 private:
  QString host_;
  std::uint16_t port_{};
  rss::protocol::PacketType last_type_{};
  std::string last_payload_;
  int sent_count_{};
  bool send_succeeds_{true};
};
