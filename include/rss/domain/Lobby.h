#pragma once

#include "rss/domain/Room.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

namespace rss::domain {

class Lobby {
public:
    std::shared_ptr<Room> createRoom(std::string name);
    std::shared_ptr<Room> findRoom(std::uint32_t room_id) const;
    void eraseIfEmpty(std::uint32_t room_id);
    [[nodiscard]] std::vector<RoomSnapshot> snapshot() const;

private:
    mutable std::mutex mutex_;
    std::uint32_t next_room_id_{1};
    std::unordered_map<std::uint32_t, std::shared_ptr<Room>> rooms_;
};

} // namespace rss::domain
