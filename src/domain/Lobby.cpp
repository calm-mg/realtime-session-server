#include "rss/domain/Lobby.h"

#include <utility>

namespace rss::domain {

std::shared_ptr<Room> Lobby::createRoom(std::string name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto room_id = next_room_id_++;
    auto room = std::make_shared<Room>(room_id, std::move(name));
    rooms_[room_id] = room;
    return room;
}

std::shared_ptr<Room> Lobby::findRoom(std::uint32_t room_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return nullptr;
    }
    return it->second;
}

void Lobby::eraseIfEmpty(std::uint32_t room_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = rooms_.find(room_id);
    if (it != rooms_.end() && it->second->empty()) {
        rooms_.erase(it);
    }
}

std::vector<RoomSnapshot> Lobby::snapshot() const
{
    std::vector<std::shared_ptr<Room>> rooms;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        rooms.reserve(rooms_.size());
        for (const auto& [_, room] : rooms_) {
            rooms.push_back(room);
        }
    }

    std::vector<RoomSnapshot> snapshots;
    snapshots.reserve(rooms.size());
    for (const auto& room : rooms) {
        snapshots.push_back(room->snapshot());
    }
    return snapshots;
}

} // namespace rss::domain
