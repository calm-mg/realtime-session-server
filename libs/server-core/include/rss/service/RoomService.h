#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "rss/domain/Lobby.h"
#include "rss/domain/Position.h"
#include "rss/domain/User.h"

namespace rss::service {

struct LoginResult {
  bool ok{};
  std::string error;
  domain::User user;
};

struct RoomActionResult {
  bool ok{};
  std::string error;
  std::uint32_t room_id{};
  domain::User actor;
  std::vector<std::uint64_t> recipients;
};

class RoomService {
 public:
  LoginResult login(std::uint64_t session_id, std::string name);
  std::optional<domain::User> userOf(std::uint64_t session_id) const;

  RoomActionResult createRoom(std::uint64_t session_id, std::string room_name);
  RoomActionResult joinRoom(std::uint64_t session_id, std::uint32_t room_id);
  RoomActionResult leaveRoom(std::uint64_t session_id);
  RoomActionResult chat(std::uint64_t session_id);
  RoomActionResult updatePosition(std::uint64_t session_id,
                                  domain::Position position);
  RoomActionResult disconnect(std::uint64_t session_id);

 private:
  RoomActionResult requireUserLocked(std::uint64_t session_id) const;

  mutable std::mutex mutex_;
  domain::Lobby lobby_;
  std::uint64_t next_user_id_{1};
  std::unordered_map<std::uint64_t, domain::User> users_by_session_;
  std::unordered_map<std::uint64_t, std::uint32_t> room_by_session_;
};

}  // namespace rss::service
