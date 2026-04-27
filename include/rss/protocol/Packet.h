#pragma once

#include "rss/protocol/PacketTypes.h"

#include <cstdint>
#include <vector>

namespace rss::protocol {

constexpr std::uint16_t kPacketHeaderSize = 4;
constexpr std::uint16_t kMaxPacketSize = 4096;

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

} // namespace rss::protocol
