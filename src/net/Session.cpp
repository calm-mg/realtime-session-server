#include "rss/net/Session.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace rss::net {

Session::Session(int fd, std::uint64_t id)
    : Session(fd, id, std::numeric_limits<std::size_t>::max()) {}

Session::Session(int fd, std::uint64_t id,
                 std::size_t max_pending_write_bytes)
    : fd_(fd), id_(id), max_pending_write_bytes_(max_pending_write_bytes) {}

int Session::fd() const { return fd_; }

std::uint64_t Session::id() const { return id_; }

protocol::PacketCodec& Session::codec() { return codec_; }

void Session::touch() { last_seen_ = std::chrono::steady_clock::now(); }

std::chrono::steady_clock::time_point Session::lastSeen() const {
  return last_seen_;
}

bool Session::tryEnqueue(std::vector<std::uint8_t> bytes) {
  if (bytes.size() > max_pending_write_bytes_ - pending_write_bytes_) {
    return false;
  }

  const auto byte_count = bytes.size();
  pending_writes_.push_back(PendingWrite{std::move(bytes), 0});
  pending_write_bytes_ += byte_count;
  return true;
}

std::size_t Session::pendingWriteBytes() const {
  return pending_write_bytes_;
}

void Session::enqueue(std::vector<std::uint8_t> bytes) {
  if (!tryEnqueue(std::move(bytes))) {
    throw std::logic_error("unbounded session pending write overflow");
  }
}

bool Session::hasPendingWrite() const { return !pending_writes_.empty(); }

PendingWrite& Session::currentWrite() { return pending_writes_.front(); }

void Session::consumeWrite(std::size_t byte_count) {
  if (pending_writes_.empty()) {
    throw std::logic_error("no pending write to consume");
  }

  auto& write = pending_writes_.front();
  const auto remaining = write.bytes.size() - write.offset;
  if (byte_count > remaining) {
    throw std::logic_error("consume exceeds pending write");
  }

  write.offset += byte_count;
  pending_write_bytes_ -= byte_count;
  if (write.offset == write.bytes.size()) {
    pending_writes_.pop_front();
  }
}

}  // namespace rss::net
