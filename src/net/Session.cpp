#include "rss/net/Session.h"

#include <utility>

namespace rss::net {

Session::Session(int fd, std::uint64_t id)
    : fd_(fd)
    , id_(id)
{
}

int Session::fd() const
{
    return fd_;
}

std::uint64_t Session::id() const
{
    return id_;
}

protocol::PacketCodec& Session::codec()
{
    return codec_;
}

void Session::touch()
{
    last_seen_ = std::chrono::steady_clock::now();
}

std::chrono::steady_clock::time_point Session::lastSeen() const
{
    return last_seen_;
}

void Session::enqueue(std::vector<std::uint8_t> bytes)
{
    pending_writes_.push_back(PendingWrite{std::move(bytes), 0});
}

bool Session::hasPendingWrite() const
{
    return !pending_writes_.empty();
}

PendingWrite& Session::currentWrite()
{
    return pending_writes_.front();
}

void Session::consumeWrite(std::size_t byte_count)
{
    auto& write = pending_writes_.front();
    write.offset += byte_count;
    if (write.offset >= write.bytes.size()) {
        pending_writes_.pop_front();
    }
}

} // namespace rss::net
