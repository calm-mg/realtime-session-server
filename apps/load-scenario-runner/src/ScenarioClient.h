#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

#include "rss/protocol/PacketCodec.h"

namespace rss::tools {

class ScenarioClient {
 public:
  ScenarioClient() = default;
  ~ScenarioClient();
  ScenarioClient(ScenarioClient&& other) noexcept;
  ScenarioClient& operator=(ScenarioClient&& other) noexcept;
  ScenarioClient(const ScenarioClient&) = delete;
  ScenarioClient& operator=(const ScenarioClient&) = delete;

  void connect(std::string_view host, std::uint16_t port,
               std::chrono::milliseconds timeout);
  void setReceiveBufferBytes(int bytes);
  void login(std::string_view name, std::chrono::milliseconds timeout);
  std::uint32_t createRoom(std::string_view name,
                           std::chrono::milliseconds timeout);
  void joinRoom(std::uint32_t room_id, std::chrono::milliseconds timeout);
  void sendChat(std::string_view payload, std::chrono::milliseconds timeout);
  rss::protocol::Packet receivePacket(std::chrono::milliseconds timeout);
  void close() noexcept;

 private:
  void sendPacket(rss::protocol::PacketType type, std::string_view payload,
                  std::chrono::milliseconds timeout);
  rss::protocol::Packet waitFor(rss::protocol::PacketType expected,
                                std::chrono::milliseconds timeout);

  int fd_{-1};
  rss::protocol::PacketCodec codec_;
};

}  // namespace rss::tools
