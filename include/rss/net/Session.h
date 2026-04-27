#pragma once

#include "rss/protocol/PacketCodec.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <vector>

namespace rss::net {

struct PendingWrite {
    std::vector<std::uint8_t> bytes;
    std::size_t offset{};
};

class Session {
public:
    Session(int fd, std::uint64_t id);

    [[nodiscard]] int fd() const;
    [[nodiscard]] std::uint64_t id() const;
    [[nodiscard]] protocol::PacketCodec& codec();

    void touch();
    [[nodiscard]] std::chrono::steady_clock::time_point lastSeen() const;

    void enqueue(std::vector<std::uint8_t> bytes);
    [[nodiscard]] bool hasPendingWrite() const;
    [[nodiscard]] PendingWrite& currentWrite();
    void consumeWrite(std::size_t byte_count);

private:
    int fd_{-1};
    std::uint64_t id_{};
    protocol::PacketCodec codec_;
    std::deque<PendingWrite> pending_writes_;
    std::chrono::steady_clock::time_point last_seen_{std::chrono::steady_clock::now()};
};

} // namespace rss::net
