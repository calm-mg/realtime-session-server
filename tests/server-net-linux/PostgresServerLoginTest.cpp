#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "rss/domain/UserId.h"
#include "rss/protocol/PacketCodec.h"

namespace {

using namespace std::chrono_literals;

std::uint16_t reserveLoopbackPort() {
  const auto fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    throw std::runtime_error("socket failed");
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(0);
  if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    ::close(fd);
    throw std::runtime_error("bind failed");
  }
  socklen_t size = sizeof(address);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) < 0) {
    ::close(fd);
    throw std::runtime_error("getsockname failed");
  }
  const auto port = ntohs(address.sin_port);
  ::close(fd);
  return port;
}

int connectTo(std::uint16_t port) {
  const auto fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return -1;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) !=
      0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

class ServerProcess {
 public:
  ServerProcess(std::uint16_t port, const char* database_url) : port_(port) {
    pid_ = ::fork();
    if (pid_ < 0) {
      throw std::runtime_error("fork failed");
    }
    if (pid_ == 0) {
      if (::setenv("RSS_DATABASE_URL", database_url, 1) != 0 ||
          ::setenv("RSS_DB_WORKERS", "1", 1) != 0 ||
          ::setenv("RSS_DB_QUEUE_CAPACITY", "16", 1) != 0) {
        ::_exit(126);
      }
      const auto port_text = std::to_string(port_);
      ::execl(RSS_SERVER_EXECUTABLE_PATH, RSS_SERVER_EXECUTABLE_PATH,
              "127.0.0.1", port_text.c_str(), "1", nullptr);
      ::_exit(127);
    }
  }

  ~ServerProcess() {
    if (pid_ <= 0) {
      return;
    }
    if (::waitpid(pid_, nullptr, WNOHANG) == 0) {
      static_cast<void>(::kill(pid_, SIGKILL));
      static_cast<void>(::waitpid(pid_, nullptr, 0));
    }
  }

  bool waitUntilListening(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      const auto fd = connectTo(port_);
      if (fd >= 0) {
        ::close(fd);
        return true;
      }
      int status{};
      if (::waitpid(pid_, &status, WNOHANG) == pid_) {
        pid_ = -1;
        return false;
      }
      std::this_thread::sleep_for(10ms);
    }
    return false;
  }

  bool stop(std::chrono::milliseconds timeout) {
    if (::kill(pid_, SIGTERM) != 0) {
      return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      int status{};
      if (::waitpid(pid_, &status, WNOHANG) == pid_) {
        pid_ = -1;
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
      }
      std::this_thread::sleep_for(10ms);
    }
    return false;
  }

 private:
  pid_t pid_{-1};
  std::uint16_t port_{};
};

void sendAll(int fd, const std::vector<std::uint8_t>& bytes) {
  std::size_t sent{};
  while (sent < bytes.size()) {
    const auto result =
        ::send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
    if (result > 0) {
      sent += static_cast<std::size_t>(result);
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    throw std::runtime_error("send failed");
  }
}

rss::protocol::Packet receivePacket(int fd, std::chrono::milliseconds timeout) {
  rss::protocol::PacketCodec codec;
  std::array<std::uint8_t, rss::protocol::kMaxPacketSize> buffer{};
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    pollfd descriptor{fd, POLLIN, 0};
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
    const auto ready =
        ::poll(&descriptor, 1, static_cast<int>(remaining.count()));
    if (ready < 0 && errno == EINTR) {
      continue;
    }
    if (ready <= 0) {
      break;
    }
    const auto received = ::recv(fd, buffer.data(), buffer.size(), 0);
    if (received <= 0) {
      throw std::runtime_error("receive failed");
    }
    codec.feed(buffer.data(), static_cast<std::size_t>(received));
    if (auto packet = codec.peekPacket()) {
      return std::move(*packet);
    }
  }
  throw std::runtime_error("receive timed out");
}

std::string loginUserId(std::uint16_t port, std::string_view name) {
  const auto fd = connectTo(port);
  if (fd < 0) {
    throw std::runtime_error("connect failed");
  }
  try {
    sendAll(fd, rss::protocol::PacketCodec::encode(
                    rss::protocol::PacketType::LoginReq, name));
    const auto response = receivePacket(fd, 5s);
    if (response.type != rss::protocol::PacketType::LoginRes) {
      throw std::runtime_error("login failed");
    }
    const auto payload = rss::protocol::payloadToString(response);
    constexpr std::string_view marker = "user_id=";
    const auto begin = payload.find(marker);
    if (begin == std::string::npos) {
      throw std::runtime_error("login response has no user id");
    }
    const auto value_begin = begin + marker.size();
    const auto value_end = payload.find('|', value_begin);
    const auto user_id = payload.substr(value_begin, value_end - value_begin);
    if (!rss::domain::UserId::parse(user_id).has_value()) {
      throw std::runtime_error("login response has invalid user id");
    }
    ::close(fd);
    return user_id;
  } catch (...) {
    ::close(fd);
    throw;
  }
}

TEST(PostgresServerLoginTest, RestartRestoresPermanentUserId) {
  const auto* database_url = std::getenv("RSS_TEST_DATABASE_URL");
  if (database_url == nullptr || *database_url == '\0') {
    GTEST_SKIP() << "RSS_TEST_DATABASE_URL is not configured";
  }
  const auto name = "restart_" + std::to_string(::getpid());

  const auto first_port = reserveLoopbackPort();
  ServerProcess first(first_port, database_url);
  ASSERT_TRUE(first.waitUntilListening(5s));
  const auto first_id = loginUserId(first_port, name);
  ASSERT_TRUE(first.stop(7s));

  const auto second_port = reserveLoopbackPort();
  ServerProcess second(second_port, database_url);
  ASSERT_TRUE(second.waitUntilListening(5s));
  const auto second_id = loginUserId(second_port, name);
  ASSERT_TRUE(second.stop(7s));

  EXPECT_EQ(first_id, second_id);
}

}  // namespace
