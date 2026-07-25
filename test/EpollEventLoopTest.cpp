#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <utility>

#include "rss/net/EpollEventLoop.h"

namespace {

class OwnedFd {
 public:
  OwnedFd() = default;
  explicit OwnedFd(int fd) : fd_(fd) {}
  ~OwnedFd() { reset(); }

  OwnedFd(const OwnedFd&) = delete;
  OwnedFd& operator=(const OwnedFd&) = delete;

  OwnedFd(OwnedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  OwnedFd& operator=(OwnedFd&& other) noexcept {
    if (this != &other) {
      reset();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }

  [[nodiscard]] int get() const { return fd_; }

  void reset(int fd = -1) {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    fd_ = fd;
  }

 private:
  int fd_{-1};
};

std::pair<OwnedFd, OwnedFd> socketPair() {
  int sockets[2]{-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0,
                   sockets) != 0) {
    return {};
  }
  return {OwnedFd(sockets[0]), OwnedFd(sockets[1])};
}

TEST(EpollEventLoopTest, KeepsRegistrationTokenInAlreadyReturnedEventBatch) {
  constexpr std::uint64_t old_token = 101;
  constexpr std::uint64_t new_token = 202;
  rss::net::EpollEventLoop loop;
  auto [old_server, old_peer] = socketPair();
  ASSERT_GE(old_server.get(), 0);
  const auto reused_fd = old_server.get();

  loop.add(old_server.get(), EPOLLIN, old_token);
  const std::uint8_t old_byte = 1;
  ASSERT_EQ(::send(old_peer.get(), &old_byte, sizeof(old_byte), MSG_NOSIGNAL),
            1);
  const auto stale_batch = loop.wait(100, 4);
  ASSERT_EQ(stale_batch.size(), 1U);
  ASSERT_EQ(stale_batch.front().data.u64, old_token);

  loop.remove(old_server.get());
  old_server.reset();
  old_peer.reset();

  auto [new_server, new_peer] = socketPair();
  ASSERT_GE(new_server.get(), 0);
  if (new_peer.get() == reused_fd) {
    std::swap(new_server, new_peer);
  }
  if (new_server.get() != reused_fd) {
    ASSERT_EQ(::dup2(new_server.get(), reused_fd), reused_fd);
    new_server.reset(reused_fd);
  }
  ASSERT_EQ(new_server.get(), reused_fd);

  loop.add(new_server.get(), EPOLLIN, new_token);
  const std::uint8_t new_byte = 2;
  ASSERT_EQ(::send(new_peer.get(), &new_byte, sizeof(new_byte), MSG_NOSIGNAL),
            1);
  const auto current_batch = loop.wait(100, 4);
  ASSERT_EQ(current_batch.size(), 1U);

  EXPECT_EQ(stale_batch.front().data.u64, old_token);
  EXPECT_EQ(current_batch.front().data.u64, new_token);
}

}  // namespace
