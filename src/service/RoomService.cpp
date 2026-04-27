#include "rss/service/RoomService.h"

#include <utility>

namespace rss::service {
namespace {

constexpr auto kDefaultRoomName = "room";

std::string normalizeName(std::string name, std::string fallback)
{
    if (name.empty()) {
        return fallback;
    }
    if (name.size() > 32) {
        name.resize(32);
    }
    return name;
}

} // namespace

domain::User RoomService::login(std::uint64_t session_id, std::string name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto existing = users_by_session_.find(session_id);
    if (existing != users_by_session_.end()) {
        existing->second.name = normalizeName(std::move(name), existing->second.name);
        return existing->second;
    }

    domain::User user;
    user.id = next_user_id_++;
    user.session_id = session_id;
    user.name = normalizeName(std::move(name), "user-" + std::to_string(user.id));
    users_by_session_[session_id] = user;
    return user;
}

std::optional<domain::User> RoomService::userOf(std::uint64_t session_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = users_by_session_.find(session_id);
    if (it == users_by_session_.end()) {
        return std::nullopt;
    }
    return it->second;
}

RoomActionResult RoomService::createRoom(std::uint64_t session_id, std::string room_name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = requireUserLocked(session_id);
    if (!result.ok) {
        return result;
    }

    leaveCurrentRoomLocked(session_id);
    auto room = lobby_.createRoom(normalizeName(std::move(room_name), kDefaultRoomName));
    room->join(result.actor);
    room_by_session_[session_id] = room->id();

    result.room_id = room->id();
    result.recipients = room->sessionIds();
    return result;
}

RoomActionResult RoomService::joinRoom(std::uint64_t session_id, std::uint32_t room_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = requireUserLocked(session_id);
    if (!result.ok) {
        return result;
    }

    auto room = lobby_.findRoom(room_id);
    if (!room) {
        result.ok = false;
        result.error = "room not found";
        return result;
    }

    leaveCurrentRoomLocked(session_id);
    room->join(result.actor);
    room_by_session_[session_id] = room_id;

    result.room_id = room_id;
    result.recipients = room->sessionIds();
    return result;
}

RoomActionResult RoomService::leaveRoom(std::uint64_t session_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = requireUserLocked(session_id);
    if (!result.ok) {
        return result;
    }

    const auto room_it = room_by_session_.find(session_id);
    if (room_it == room_by_session_.end()) {
        result.ok = false;
        result.error = "user is not in a room";
        return result;
    }

    const auto room_id = room_it->second;
    auto room = lobby_.findRoom(room_id);
    if (!room) {
        room_by_session_.erase(room_it);
        result.ok = false;
        result.error = "room not found";
        return result;
    }

    result.room_id = room_id;
    result.recipients = room->sessionIds();
    room->leave(session_id);
    room_by_session_.erase(room_it);
    lobby_.eraseIfEmpty(room_id);
    return result;
}

RoomActionResult RoomService::chat(std::uint64_t session_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = requireUserLocked(session_id);
    if (!result.ok) {
        return result;
    }

    const auto room_it = room_by_session_.find(session_id);
    if (room_it == room_by_session_.end()) {
        result.ok = false;
        result.error = "user is not in a room";
        return result;
    }

    auto room = lobby_.findRoom(room_it->second);
    if (!room) {
        result.ok = false;
        result.error = "room not found";
        return result;
    }

    result.room_id = room_it->second;
    result.recipients = room->sessionIds();
    return result;
}

RoomActionResult RoomService::updatePosition(std::uint64_t session_id, domain::Position position)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = requireUserLocked(session_id);
    if (!result.ok) {
        return result;
    }

    const auto room_it = room_by_session_.find(session_id);
    if (room_it == room_by_session_.end()) {
        result.ok = false;
        result.error = "user is not in a room";
        return result;
    }

    auto room = lobby_.findRoom(room_it->second);
    if (!room) {
        result.ok = false;
        result.error = "room not found";
        return result;
    }

    room->setPosition(session_id, position);
    result.room_id = room_it->second;
    result.recipients = room->sessionIds();
    return result;
}

RoomActionResult RoomService::disconnect(std::uint64_t session_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    RoomActionResult result;
    const auto user_it = users_by_session_.find(session_id);
    if (user_it == users_by_session_.end()) {
        result.ok = false;
        result.error = "unknown user";
        return result;
    }
    result.ok = true;
    result.actor = user_it->second;

    const auto room_it = room_by_session_.find(session_id);
    if (room_it != room_by_session_.end()) {
        const auto room_id = room_it->second;
        auto room = lobby_.findRoom(room_id);
        if (room) {
            result.room_id = room_id;
            result.recipients = room->sessionIds();
            room->leave(session_id);
            lobby_.eraseIfEmpty(room_id);
        }
        room_by_session_.erase(room_it);
    }

    users_by_session_.erase(user_it);
    return result;
}

RoomActionResult RoomService::requireUserLocked(std::uint64_t session_id) const
{
    RoomActionResult result;
    const auto it = users_by_session_.find(session_id);
    if (it == users_by_session_.end()) {
        result.ok = false;
        result.error = "login required";
        return result;
    }

    result.ok = true;
    result.actor = it->second;
    return result;
}

void RoomService::leaveCurrentRoomLocked(std::uint64_t session_id)
{
    const auto room_it = room_by_session_.find(session_id);
    if (room_it == room_by_session_.end()) {
        return;
    }

    const auto room_id = room_it->second;
    auto room = lobby_.findRoom(room_id);
    if (room) {
        room->leave(session_id);
        lobby_.eraseIfEmpty(room_id);
    }
    room_by_session_.erase(room_it);
}

} // namespace rss::service
