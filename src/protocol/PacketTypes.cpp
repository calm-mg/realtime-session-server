#include "rss/protocol/PacketTypes.h"

namespace rss::protocol {

std::string_view toString(PacketType type)
{
    switch (type) {
    case PacketType::LoginReq:
        return "LOGIN_REQ";
    case PacketType::LoginRes:
        return "LOGIN_RES";
    case PacketType::CreateRoomReq:
        return "CREATE_ROOM_REQ";
    case PacketType::CreateRoomRes:
        return "CREATE_ROOM_RES";
    case PacketType::JoinRoomReq:
        return "JOIN_ROOM_REQ";
    case PacketType::JoinRoomRes:
        return "JOIN_ROOM_RES";
    case PacketType::LeaveRoomReq:
        return "LEAVE_ROOM_REQ";
    case PacketType::LeaveRoomRes:
        return "LEAVE_ROOM_RES";
    case PacketType::ChatReq:
        return "CHAT_REQ";
    case PacketType::PositionUpdate:
        return "POSITION_UPDATE";
    case PacketType::RoomBroadcast:
        return "ROOM_BROADCAST";
    case PacketType::Ping:
        return "PING";
    case PacketType::Pong:
        return "PONG";
    case PacketType::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

} // namespace rss::protocol
