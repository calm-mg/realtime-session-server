#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "rss/net/ServerConfig.h"
#include "rss/net/TcpServer.h"
#include "rss/protocol/PacketCodec.h"
#include "rss/service/SessionEventHandler.h"

namespace {

using namespace std::chrono_literals;
using rss::protocol::PacketCodec;
using rss::protocol::PacketType;
using rss::service::OutboundMessage;
using rss::service::SessionEvent;
using rss::service::SessionEventKind;

template <typename Predicate>
bool waitUntil(Predicate&& predicate,
               std::chrono::steady_clock::duration timeout = 1s) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return predicate();
}

template <typename Predicate>
bool remainsTrueFor(Predicate&& predicate,
                    std::chrono::steady_clock::duration duration) {
  const auto deadline = std::chrono::steady_clock::now() + duration;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!predicate()) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  return predicate();
}

class ClientSocket {
 public:
  ClientSocket() = default;
  explicit ClientSocket(int fd) : fd_(fd) {}

  ~ClientSocket() { reset(); }

  ClientSocket(const ClientSocket&) = delete;
  ClientSocket& operator=(const ClientSocket&) = delete;

  ClientSocket(ClientSocket&& other) noexcept
      : fd_(std::exchange(other.fd_, -1)) {}

  ClientSocket& operator=(ClientSocket&& other) noexcept {
    if (this != &other) {
      reset();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }

  [[nodiscard]] bool valid() const { return fd_ >= 0; }
  [[nodiscard]] int get() const { return fd_; }

  void reset() noexcept {
    if (fd_ < 0) {
      return;
    }
    ::shutdown(fd_, SHUT_RDWR);
    ::close(fd_);
    fd_ = -1;
  }

 private:
  int fd_{-1};
};

ClientSocket connectClient(std::uint16_t port) {
  const auto fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return {};
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) <
      0) {
    ::close(fd);
    return {};
  }
  return ClientSocket(fd);
}

bool sendSingleWrite(int fd, const std::vector<std::uint8_t>& bytes) {
  ssize_t sent = -1;
  do {
    sent = ::send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
  } while (sent < 0 && errno == EINTR);
  return sent == static_cast<ssize_t>(bytes.size());
}

std::vector<std::uint8_t> encodePingBatch(
    const std::vector<std::string>& payloads) {
  std::vector<std::uint8_t> bytes;
  for (const auto& payload : payloads) {
    auto frame = PacketCodec::encode(PacketType::Ping, payload);
    bytes.insert(bytes.end(), frame.begin(), frame.end());
  }
  return bytes;
}

std::vector<rss::protocol::Packet> receivePackets(
    int fd, std::size_t expected_count,
    std::chrono::steady_clock::duration timeout = 2s) {
  PacketCodec codec;
  std::vector<rss::protocol::Packet> packets;
  const auto deadline = std::chrono::steady_clock::now() + timeout;

  while (packets.size() < expected_count &&
         std::chrono::steady_clock::now() < deadline) {
    pollfd entry{};
    entry.fd = fd;
    entry.events = POLLIN;
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
    const auto poll_timeout =
        static_cast<int>(std::max<std::int64_t>(1, remaining.count()));
    const auto ready = ::poll(&entry, 1, poll_timeout);
    if (ready < 0 && errno == EINTR) {
      continue;
    }
    if (ready <= 0) {
      break;
    }

    std::uint8_t buffer[4096];
    const auto received = ::recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (received <= 0) {
      break;
    }
    codec.feed(buffer, static_cast<std::size_t>(received));
    while (auto packet = codec.peekPacket()) {
      packets.push_back(std::move(*packet));
      codec.consumePacket();
    }
  }
  return packets;
}

bool waitForPeerClose(int fd,
                      std::chrono::steady_clock::duration timeout = 1s) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    pollfd entry{};
    entry.fd = fd;
    entry.events = POLLIN;
    const auto ready = ::poll(&entry, 1, 10);
    if (ready < 0 && errno == EINTR) {
      continue;
    }
    if (ready <= 0) {
      continue;
    }

    std::uint8_t byte{};
    const auto received = ::recv(fd, &byte, sizeof(byte), MSG_DONTWAIT);
    if (received == 0) {
      return true;
    }
    if (received < 0 &&
        (errno == ECONNRESET || errno == ENOTCONN || errno == EPIPE)) {
      return true;
    }
  }
  return false;
}

class BlockingPongHandler final : public rss::service::SessionEventHandler {
 public:
  std::vector<OutboundMessage> handle(const SessionEvent& event) override {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      ++entered_count_;
      changed_.notify_all();
      released_.wait(lock, [this] { return release_requested_; });

      if (event.kind == SessionEventKind::Disconnected) {
        ++disconnected_count_;
        changed_.notify_all();
        return {};
      }

      handled_payloads_.push_back(rss::protocol::payloadToString(event.packet));
      changed_.notify_all();
    }

    return {
        {event.session_id,
         PacketCodec::encode(PacketType::Pong,
                             rss::protocol::payloadToString(event.packet))}};
  }

  void release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      release_requested_ = true;
    }
    released_.notify_all();
  }

  bool waitForEnteredCount(std::size_t count,
                           std::chrono::steady_clock::duration timeout = 1s) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout,
                             [this, count] { return entered_count_ >= count; });
  }

  bool waitForDisconnectedCount(
      std::size_t count, std::chrono::steady_clock::duration timeout = 1s) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(
        lock, timeout, [this, count] { return disconnected_count_ >= count; });
  }

  [[nodiscard]] std::size_t disconnectedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return disconnected_count_;
  }

  [[nodiscard]] std::vector<std::string> handledPayloads() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return handled_payloads_;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable released_;
  std::condition_variable changed_;
  bool release_requested_{false};
  std::size_t entered_count_{0};
  std::size_t disconnected_count_{0};
  std::vector<std::string> handled_payloads_;
};

class TcpServerBackpressureTest : public testing::Test {
 protected:
  ~TcpServerBackpressureTest() override {
    handler_.release();
    if (server_ != nullptr) {
      server_->stop();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
  }

  static rss::net::ServerConfig loopbackConfig() {
    rss::net::ServerConfig config;
    config.host = "127.0.0.1";
    config.port = 0;
    config.worker_count = 1;
    config.max_events = 32;
    return config;
  }

  bool startServer(rss::net::ServerConfig config) {
    server_ =
        std::make_unique<rss::net::TcpServer>(std::move(config), &handler_);
    server_thread_ = std::thread([this] {
      try {
        server_->run();
      } catch (...) {
        std::lock_guard<std::mutex> lock(server_error_mutex_);
        server_error_ = std::current_exception();
      }
    });

    return waitUntil(
               [this] { return server_->boundPort() != 0 || serverFailed(); },
               1s) &&
           server_->boundPort() != 0;
  }

  [[nodiscard]] bool serverFailed() const {
    std::lock_guard<std::mutex> lock(server_error_mutex_);
    return server_error_ != nullptr;
  }

  [[nodiscard]] std::uint16_t boundPort() const { return server_->boundPort(); }

  BlockingPongHandler handler_;
  std::unique_ptr<rss::net::TcpServer> server_;
  std::thread server_thread_;
  mutable std::mutex server_error_mutex_;
  std::exception_ptr server_error_;
};

TEST_F(TcpServerBackpressureTest,
       RejectsOnlyNewConnectionAtMaximumSessionCount) {
  auto config = loopbackConfig();
  config.max_sessions = 1;
  handler_.release();
  ASSERT_TRUE(startServer(config));

  auto first = connectClient(boundPort());
  ASSERT_TRUE(first.valid());
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 1; }));

  auto second = connectClient(boundPort());
  ASSERT_TRUE(second.valid());
  ASSERT_TRUE(waitUntil([this] {
    const auto snapshot = server_->overloadSnapshot();
    return snapshot.current_sessions == 1 && snapshot.rejected_connections == 1;
  }));
  EXPECT_TRUE(waitForPeerClose(second.get()));

  const std::vector<std::string> payloads{"still-open"};
  ASSERT_TRUE(sendSingleWrite(first.get(), encodePingBatch(payloads)));
  const auto packets = receivePackets(first.get(), 1);
  ASSERT_EQ(packets.size(), 1U);
  EXPECT_EQ(packets[0].type, PacketType::Pong);
  EXPECT_EQ(rss::protocol::payloadToString(packets[0]), "still-open");
}

TEST_F(TcpServerBackpressureTest,
       PreservesPacketOrderAcrossReadPauseAndResume) {
  auto config = loopbackConfig();
  config.inbound_queue_capacity = 4;
  config.inbound_high_watermark = 3;
  config.inbound_low_watermark = 1;
  ASSERT_TRUE(startServer(config));

  auto client = connectClient(boundPort());
  ASSERT_TRUE(client.valid());
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 1; }));

  ASSERT_TRUE(sendSingleWrite(client.get(), encodePingBatch({"gate"})));
  ASSERT_TRUE(handler_.waitForEnteredCount(1));

  const std::vector<std::string> batch{"one",  "two",  "three",
                                       "four", "five", "six"};
  ASSERT_TRUE(sendSingleWrite(client.get(), encodePingBatch(batch)));
  ASSERT_TRUE(waitUntil([this] {
    const auto snapshot = server_->overloadSnapshot();
    return snapshot.read_pauses >= 1 && snapshot.max_inbound_queue_size >= 3;
  }));

  handler_.release();
  const auto packets = receivePackets(client.get(), batch.size() + 1, 3s);
  ASSERT_EQ(packets.size(), batch.size() + 1);

  const std::vector<std::string> expected{"gate", "one",  "two", "three",
                                          "four", "five", "six"};
  for (std::size_t index = 0; index < packets.size(); ++index) {
    EXPECT_EQ(packets[index].type, PacketType::Pong);
    EXPECT_EQ(rss::protocol::payloadToString(packets[index]), expected[index]);
  }

  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().read_resumes >= 1; }));
  EXPECT_EQ(handler_.handledPayloads(), expected);
}

TEST_F(TcpServerBackpressureTest,
       DeliversDeferredDisconnectedEventExactlyOnceAfterCapacityReturns) {
  auto config = loopbackConfig();
  config.inbound_queue_capacity = 4;
  config.inbound_high_watermark = 4;
  config.inbound_low_watermark = 1;
  config.max_sessions = 2;
  ASSERT_TRUE(startServer(config));

  auto packet_client = connectClient(boundPort());
  auto closing_client = connectClient(boundPort());
  ASSERT_TRUE(packet_client.valid());
  ASSERT_TRUE(closing_client.valid());
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 2; }));

  ASSERT_TRUE(sendSingleWrite(packet_client.get(), encodePingBatch({"gate"})));
  ASSERT_TRUE(handler_.waitForEnteredCount(1));

  const std::vector<std::string> batch{"one", "two", "three", "four"};
  ASSERT_TRUE(sendSingleWrite(packet_client.get(), encodePingBatch(batch)));
  ASSERT_TRUE(waitUntil([this] {
    const auto snapshot = server_->overloadSnapshot();
    return snapshot.read_pauses >= 1 && snapshot.max_inbound_queue_size == 4;
  }));

  closing_client.reset();
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 1; }));
  ASSERT_GE(server_->overloadSnapshot().inbound_queue_full, 1U);

  handler_.release();
  const auto packets = receivePackets(packet_client.get(), batch.size() + 1);
  ASSERT_EQ(packets.size(), batch.size() + 1);
  ASSERT_TRUE(handler_.waitForDisconnectedCount(1));
  ASSERT_TRUE(waitUntil([this] {
    return server_->overloadSnapshot().current_inbound_queue_size == 0;
  }));
  EXPECT_TRUE(remainsTrueFor(
      [this] { return handler_.disconnectedCount() == 1; }, 100ms));
}

}  // namespace
