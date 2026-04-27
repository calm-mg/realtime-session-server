#include "rss/protocol/PacketCodec.h"

#include <bit>
#include <limits>
#include <utility>

namespace rss::protocol {
namespace {

std::uint16_t readU16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) |
                                      static_cast<std::uint16_t>(data[1]));
}

std::uint32_t readU32(const std::uint8_t* data)
{
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

void writeU16(std::vector<std::uint8_t>& out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void writeU32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

float decodeFloat(const std::uint8_t* data)
{
    const auto bits = readU32(data);
    return std::bit_cast<float>(bits);
}

void encodeFloat(std::vector<std::uint8_t>& out, float value)
{
    writeU32(out, std::bit_cast<std::uint32_t>(value));
}

} // namespace

ProtocolError::ProtocolError(const char* message)
    : std::runtime_error(message)
{
}

ProtocolError::ProtocolError(const std::string& message)
    : std::runtime_error(message)
{
}

std::vector<std::uint8_t> PacketCodec::encode(PacketType type, std::span<const std::uint8_t> payload)
{
    const auto total_size = payload.size() + kPacketHeaderSize;
    if (total_size > kMaxPacketSize || total_size > std::numeric_limits<std::uint16_t>::max()) {
        throw ProtocolError("packet payload is too large");
    }

    std::vector<std::uint8_t> out;
    out.reserve(total_size);
    writeU16(out, static_cast<std::uint16_t>(total_size));
    writeU16(out, static_cast<std::uint16_t>(type));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<std::uint8_t> PacketCodec::encode(PacketType type, std::string_view payload)
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    return encode(type, std::span<const std::uint8_t>(bytes, payload.size()));
}

std::vector<std::uint8_t> PacketCodec::encodePosition(float x, float y)
{
    std::vector<std::uint8_t> out;
    out.reserve(8);
    encodeFloat(out, x);
    encodeFloat(out, y);
    return out;
}

std::pair<float, float> PacketCodec::decodePosition(std::span<const std::uint8_t> payload)
{
    if (payload.size() != 8) {
        throw ProtocolError("position payload must be exactly 8 bytes");
    }
    return {decodeFloat(payload.data()), decodeFloat(payload.data() + 4)};
}

void PacketCodec::feed(const std::uint8_t* data, std::size_t size)
{
    if (size == 0) {
        return;
    }
    if (data == nullptr && size != 0) {
        throw ProtocolError("null input buffer");
    }
    buffer_.insert(buffer_.end(), data, data + size);
}

std::vector<Packet> PacketCodec::drainPackets()
{
    std::vector<Packet> packets;
    std::size_t offset = 0;

    while (buffer_.size() - offset >= kPacketHeaderSize) {
        const auto* header = buffer_.data() + offset;
        const auto packet_size = readU16(header);
        const auto packet_type = readU16(header + 2);

        if (packet_size < kPacketHeaderSize) {
            throw ProtocolError("invalid packet size");
        }
        if (packet_size > kMaxPacketSize) {
            throw ProtocolError("packet exceeds max size");
        }
        if (buffer_.size() - offset < packet_size) {
            break;
        }

        Packet packet;
        packet.type = static_cast<PacketType>(packet_type);
        const auto payload_begin = offset + kPacketHeaderSize;
        const auto payload_end = offset + packet_size;
        packet.payload.assign(buffer_.begin() + static_cast<std::ptrdiff_t>(payload_begin),
                              buffer_.begin() + static_cast<std::ptrdiff_t>(payload_end));
        packets.push_back(std::move(packet));
        offset += packet_size;
    }

    if (offset != 0) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    return packets;
}

std::string payloadToString(const Packet& packet)
{
    return {packet.payload.begin(), packet.payload.end()};
}

} // namespace rss::protocol
