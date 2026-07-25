#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rss/protocol/Packet.h"

namespace rss::protocol {

class ProtocolError final : public std::runtime_error {
 public:
  explicit ProtocolError(const char* message);
  explicit ProtocolError(const std::string& message);
};

class PacketCodec {
 public:
  static std::vector<std::uint8_t> encode(
      PacketType type, std::span<const std::uint8_t> payload);
  static std::vector<std::uint8_t> encode(PacketType type,
                                          std::string_view payload);

  static std::vector<std::uint8_t> encodePosition(float x, float y);
  static std::pair<float, float> decodePosition(
      std::span<const std::uint8_t> payload);

  void feed(const std::uint8_t* data, std::size_t size);
  [[nodiscard]] std::optional<Packet> peekPacket() const;
  void consumePacket();
  [[nodiscard]] std::vector<Packet> drainPackets();

 private:
  struct PacketFrame {
    std::size_t size;
    PacketType type;
  };

  [[nodiscard]] std::optional<PacketFrame> firstPacketFrame() const;

  std::vector<std::uint8_t> buffer_;
};

std::string payloadToString(const Packet& packet);

}  // namespace rss::protocol
