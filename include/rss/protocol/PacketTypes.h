#pragma once

#include <cstdint>
#include <string_view>

namespace rss::protocol {

enum class PacketType : std::uint16_t {
    LoginReq = 1,
    LoginRes = 2,
    CreateRoomReq = 10,
    CreateRoomRes = 11,
    JoinRoomReq = 12,
    JoinRoomRes = 13,
    LeaveRoomReq = 14,
    LeaveRoomRes = 15,
    ChatReq = 20,
    PositionUpdate = 21,
    RoomBroadcast = 22,
    Ping = 30,
    Pong = 31,
    Error = 100,
};

std::string_view toString(PacketType type);

} // namespace rss::protocol
