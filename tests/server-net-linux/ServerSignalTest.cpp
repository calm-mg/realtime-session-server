#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

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

bool canConnect(std::uint16_t port) {
  const auto fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return false;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  const auto connected = ::connect(fd, reinterpret_cast<sockaddr*>(&address),
                                   sizeof(address)) == 0;
  ::close(fd);
  return connected;
}

struct ExitResult {
  bool exited{};
  int status{};
};

class ServerProcess {
 public:
  explicit ServerProcess(std::uint16_t port) : port_(port) {
    pid_ = ::fork();
    if (pid_ < 0) {
      throw std::runtime_error("fork failed");
    }
    if (pid_ == 0) {
      const auto* database_url = std::getenv("RSS_TEST_DATABASE_URL");
      if (database_url == nullptr ||
          ::setenv("RSS_DATABASE_URL", database_url, 1) != 0) {
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

    const auto wait_result = ::waitpid(pid_, nullptr, WNOHANG);
    if (wait_result == 0) {
      static_cast<void>(::kill(pid_, SIGKILL));
      static_cast<void>(::waitpid(pid_, nullptr, 0));
    }
  }

  ServerProcess(const ServerProcess&) = delete;
  ServerProcess& operator=(const ServerProcess&) = delete;

  bool waitUntilListening(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (canConnect(port_)) {
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

  bool sendSignal(int signal_number) const {
    return ::kill(pid_, signal_number) == 0;
  }

  ExitResult waitForExit(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      int status{};
      const auto result = ::waitpid(pid_, &status, WNOHANG);
      if (result == pid_) {
        pid_ = -1;
        return {.exited = true, .status = status};
      }
      if (result < 0 && errno != EINTR) {
        throw std::runtime_error(std::string("waitpid failed: ") +
                                 std::strerror(errno));
      }
      std::this_thread::sleep_for(10ms);
    }
    return {};
  }

 private:
  pid_t pid_{-1};
  std::uint16_t port_{};
};

class ServerSignalTest : public testing::TestWithParam<int> {};

TEST_P(ServerSignalTest, StopsThroughGracefulPath) {
  if (std::getenv("RSS_TEST_DATABASE_URL") == nullptr) {
    GTEST_SKIP() << "RSS_TEST_DATABASE_URL is not configured";
  }
  ServerProcess server(reserveLoopbackPort());
  ASSERT_TRUE(server.waitUntilListening(2s));
  ASSERT_TRUE(server.sendSignal(GetParam()));

  const auto result = server.waitForExit(7s);
  ASSERT_TRUE(result.exited);
  ASSERT_TRUE(WIFEXITED(result.status));
  EXPECT_EQ(WEXITSTATUS(result.status), EXIT_SUCCESS);
}

INSTANTIATE_TEST_SUITE_P(TerminationSignals, ServerSignalTest,
                         testing::Values(SIGINT, SIGTERM),
                         [](const testing::TestParamInfo<int>& info) {
                           return info.param == SIGINT ? "Sigint" : "Sigterm";
                         });

}  // namespace
