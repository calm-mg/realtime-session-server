#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rss/protocol/PacketTypes.h"

namespace rss::protocol {

constexpr std::uint16_t kPacketHeaderSize = 4;
constexpr std::uint16_t kMaxPacketSize = 4096;
// Text limits use raw UTF-8 bytes after trimming and before percent encoding.
constexpr std::size_t kMaxUserNameBytes = 32;
constexpr std::size_t kMaxRoomNameBytes = 32;
constexpr std::size_t kMaxChatMessageBytes = 1291;

#pragma pack(push, 1)
struct PacketHeader {
  std::uint16_t size;
  std::uint16_t type;
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == kPacketHeaderSize);

struct Packet {
  PacketType type{};
  std::vector<std::uint8_t> payload;
};

}  // namespace rss::protocol
