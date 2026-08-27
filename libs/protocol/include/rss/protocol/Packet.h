#pragma once

#include <cstdint>
#include <vector>

#include "rss/protocol/PacketTypes.h"

namespace rss::protocol {

constexpr std::uint16_t kPacketHeaderSize = 4;
constexpr std::uint16_t kMaxPacketSize = 4096;
// Leaves room for the largest CHAT broadcast envelope: a uint32 room id,
// canonical UUID, uint64 session id, and 32-byte display name.
constexpr std::uint16_t kMaxChatMessageBytes =
    kMaxPacketSize - kPacketHeaderSize - 153;

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
