#pragma once

#include "rss/domain/Position.h"
#include "rss/domain/User.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rss::domain {

struct RoomSnapshot {
    std::uint32_t id{};
    std::string name;
    std::vector<User> members;
};

class Room {
public:
    Room(std::uint32_t id, std::string name);

    [[nodiscard]] std::uint32_t id() const;
    [[nodiscard]] std::string name() const;

    void join(User user);
    std::optional<User> leave(std::uint64_t session_id);
    [[nodiscard]] bool has(std::uint64_t session_id) const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::vector<std::uint64_t> sessionIds() const;
    [[nodiscard]] std::vector<User> members() const;

    void setPosition(std::uint64_t session_id, Position position);
    [[nodiscard]] std::optional<Position> positionOf(std::uint64_t session_id) const;
    [[nodiscard]] RoomSnapshot snapshot() const;

private:
    std::uint32_t id_{};
    std::string name_;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, User> members_;
    std::unordered_map<std::uint64_t, Position> positions_;
};

} // namespace rss::domain
