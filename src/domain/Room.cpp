#include "rss/domain/Room.h"

#include <utility>

namespace rss::domain {

Room::Room(std::uint32_t id, std::string name)
    : id_(id)
    , name_(std::move(name))
{
}

std::uint32_t Room::id() const
{
    return id_;
}

std::string Room::name() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return name_;
}

void Room::join(User user)
{
    std::lock_guard<std::mutex> lock(mutex_);
    members_[user.session_id] = std::move(user);
}

std::optional<User> Room::leave(std::uint64_t session_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = members_.find(session_id);
    if (it == members_.end()) {
        return std::nullopt;
    }
    User user = std::move(it->second);
    members_.erase(it);
    positions_.erase(session_id);
    return user;
}

bool Room::has(std::uint64_t session_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return members_.contains(session_id);
}

bool Room::empty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return members_.empty();
}

std::vector<std::uint64_t> Room::sessionIds() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::uint64_t> ids;
    ids.reserve(members_.size());
    for (const auto& [session_id, _] : members_) {
        ids.push_back(session_id);
    }
    return ids;
}

std::vector<User> Room::members() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<User> users;
    users.reserve(members_.size());
    for (const auto& [_, user] : members_) {
        users.push_back(user);
    }
    return users;
}

void Room::setPosition(std::uint64_t session_id, Position position)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (members_.contains(session_id)) {
        positions_[session_id] = position;
    }
}

std::optional<Position> Room::positionOf(std::uint64_t session_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = positions_.find(session_id);
    if (it == positions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

RoomSnapshot Room::snapshot() const
{
    RoomSnapshot snapshot;
    snapshot.id = id_;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot.name = name_;
        snapshot.members.reserve(members_.size());
        for (const auto& [_, user] : members_) {
            snapshot.members.push_back(user);
        }
    }
    return snapshot;
}

} // namespace rss::domain
