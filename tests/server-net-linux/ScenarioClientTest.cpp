#include <arpa/inet.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "EmbeddedServer.h"
#include "ScenarioClient.h"
#include "rss/net/ServerConfig.h"
#include "rss/protocol/PacketCodec.h"

namespace {

using namespace std::chrono_literals;

class RawLoopbackPeer {
 public:
  RawLoopbackPeer() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ == -1) {
      throw systemError("socket");
    }

    try {
      sockaddr_in address{};
      address.sin_family = AF_INET;
      address.sin_port = 0;
      address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address),
                 sizeof(address)) == -1) {
        throw systemError("bind");
      }
      if (::listen(listen_fd_, 1) == -1) {
        throw systemError("listen");
      }

      socklen_t address_size = sizeof(address);
      if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address),
                        &address_size) == -1) {
        throw systemError("getsockname");
      }
      port_ = ntohs(address.sin_port);
    } catch (...) {
      ::close(listen_fd_);
      listen_fd_ = -1;
      throw;
    }
  }

  ~RawLoopbackPeer() {
    closeFd(peer_fd_);
    closeFd(listen_fd_);
  }

  RawLoopbackPeer(const RawLoopbackPeer&) = delete;
  RawLoopbackPeer& operator=(const RawLoopbackPeer&) = delete;

  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

  void acceptClient(std::chrono::milliseconds timeout) {
    if (peer_fd_ != -1) {
      throw std::logic_error("raw loopback peer already accepted a client");
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    waitFor(listen_fd_, POLLIN, deadline, "accept");
    while (peer_fd_ == -1) {
      peer_fd_ =
          ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
      if (peer_fd_ == -1 && errno != EINTR) {
        throw systemError("accept4");
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        throw std::runtime_error("accept timed out");
      }
    }
  }

  void sendSingleWrite(std::span<const std::uint8_t> bytes,
                       std::chrono::milliseconds timeout) {
    if (peer_fd_ == -1) {
      throw std::logic_error("raw loopback peer has no client");
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    waitFor(peer_fd_, POLLOUT, deadline, "send");
    while (true) {
      const auto sent =
          ::send(peer_fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
      if (sent == static_cast<ssize_t>(bytes.size())) {
        return;
      }
      if (sent >= 0) {
        throw std::runtime_error("raw loopback peer sent a partial write");
      }
      if (errno != EINTR) {
        throw systemError("send");
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        throw std::runtime_error("send timed out");
      }
    }
  }

 private:
  using Deadline = std::chrono::steady_clock::time_point;

  static std::runtime_error systemError(std::string_view operation) {
    return std::runtime_error(std::string(operation) + ": " +
                              std::strerror(errno));
  }

  static void closeFd(int& fd) noexcept {
    if (fd != -1) {
      ::close(fd);
      fd = -1;
    }
  }

  static void waitFor(int fd, short events, Deadline deadline,
                      std::string_view operation) {
    while (true) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        throw std::runtime_error(std::string(operation) + " timed out");
      }
      const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
      const auto timeout =
          static_cast<int>(std::max<std::int64_t>(1, remaining.count()));
      pollfd descriptor{fd, events, 0};
      const auto ready = ::poll(&descriptor, 1, timeout);
      if (ready > 0 && (descriptor.revents & events) != 0) {
        return;
      }
      if (ready == 0) {
        throw std::runtime_error(std::string(operation) + " timed out");
      }
      if (ready < 0 && errno == EINTR) {
        continue;
      }
      if (ready < 0) {
        throw systemError("poll");
      }
      throw std::runtime_error(std::string(operation) +
                               ": socket became unavailable");
    }
  }

  int listen_fd_{-1};
  int peer_fd_{-1};
  std::uint16_t port_{};
};

std::unique_ptr<rss::tools::EmbeddedServer> startTestServer() {
  rss::net::ServerConfig config;
  config.host = "127.0.0.1";
  config.port = 0;
  config.worker_count = 1;

  auto server = std::make_unique<rss::tools::EmbeddedServer>(config);
  server->start(2s);
  return server;
}

TEST(ScenarioClientTest, LogsInCreatesRoomAndReceivesOwnChat) {
  auto server = startTestServer();
  rss::tools::ScenarioClient client;
  client.connect("127.0.0.1", server->port(), 2s);
  client.login("alice", 2s);
  const auto room_id = client.createRoom("room", 2s);
  ASSERT_NE(room_id, 0U);

  client.sendChat("run=1;sender=0;seq=0;sent_us=1", 2s);
  const auto packet = client.receivePacket(2s);
  EXPECT_EQ(packet.type, rss::protocol::PacketType::RoomBroadcast);
  EXPECT_NE(rss::protocol::payloadToString(packet).find("event=CHAT"),
            std::string::npos);
}

TEST(ScenarioClientTest, ReturnsRealServerJoinAndChatPacketsInOrder) {
  auto server = startTestServer();
  rss::tools::ScenarioClient owner;
  owner.connect("127.0.0.1", server->port(), 2s);
  owner.setReceiveBufferBytes(256);
  owner.login("owner", 2s);
  const auto room_id = owner.createRoom("room", 2s);

  rss::tools::ScenarioClient guest;
  guest.connect("127.0.0.1", server->port(), 2s);
  guest.login("guest", 2s);
  guest.joinRoom(room_id, 2s);
  const std::string message =
      "run=1;sender=1;seq=0;sent_us=1;" + std::string(3000, 'x');
  guest.sendChat(message, 2s);

  const auto joined = owner.receivePacket(2s);
  ASSERT_EQ(joined.type, rss::protocol::PacketType::RoomBroadcast);
  EXPECT_NE(rss::protocol::payloadToString(joined).find("event=JOIN|"),
            std::string::npos);

  const auto chat = owner.receivePacket(2s);
  ASSERT_EQ(chat.type, rss::protocol::PacketType::RoomBroadcast);
  const auto chat_payload = rss::protocol::payloadToString(chat);
  EXPECT_NE(chat_payload.find("event=CHAT|"), std::string::npos);
  EXPECT_NE(chat_payload.find(message), std::string::npos);
}

TEST(ScenarioClientTest, WaitsForRemainderOfSplitFrame) {
  RawLoopbackPeer peer;
  const auto frame = rss::protocol::PacketCodec::encode(
      rss::protocol::PacketType::Pong, "split-frame");
  const auto split = frame.size() / 2;

  std::atomic<std::size_t> receive_calls{};
  std::atomic<bool> first_chunk_signaled{};
  std::promise<void> first_chunk_received_promise;
  auto first_chunk_received = first_chunk_received_promise.get_future();
  rss::tools::ScenarioClient client(
      [&](int fd, std::uint8_t* buffer, std::size_t capacity) {
        ++receive_calls;
        const auto received = ::recv(fd, buffer, capacity, 0);
        if (received > 0 && !first_chunk_signaled.exchange(true)) {
          first_chunk_received_promise.set_value();
        }
        return received;
      });
  client.connect("127.0.0.1", peer.port(), 2s);
  peer.acceptClient(2s);

  peer.sendSingleWrite(std::span(frame).first(split), 2s);
  auto packet_future =
      std::async(std::launch::async, [&] { return client.receivePacket(2s); });
  ASSERT_EQ(first_chunk_received.wait_for(2s), std::future_status::ready);

  peer.sendSingleWrite(std::span(frame).subspan(split), 2s);
  ASSERT_EQ(packet_future.wait_for(2s), std::future_status::ready);
  const auto packet = packet_future.get();
  EXPECT_EQ(packet.type, rss::protocol::PacketType::Pong);
  EXPECT_EQ(rss::protocol::payloadToString(packet), "split-frame");
  EXPECT_GE(receive_calls.load(), 2U);
}

TEST(ScenarioClientTest, ReturnsTwoFramesFromOneWriteInOrder) {
  RawLoopbackPeer peer;
  auto batch = rss::protocol::PacketCodec::encode(
      rss::protocol::PacketType::Pong, "first");
  const auto second = rss::protocol::PacketCodec::encode(
      rss::protocol::PacketType::RoomBroadcast, "second");
  batch.insert(batch.end(), second.begin(), second.end());

  std::atomic<std::size_t> receive_calls{};
  rss::tools::ScenarioClient client(
      [&](int fd, std::uint8_t* buffer, std::size_t capacity) {
        const auto call = receive_calls.fetch_add(1) + 1;
        if (call != 1) {
          return ::recv(fd, buffer, capacity, 0);
        }
        if (capacity < batch.size()) {
          throw std::runtime_error("receive buffer is smaller than batch");
        }

        const auto flags = ::fcntl(fd, F_GETFL, 0);
        if (flags == -1 || ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
          throw std::runtime_error("failed to make test socket blocking");
        }
        const auto received = ::recv(fd, buffer, batch.size(), MSG_WAITALL);
        if (::fcntl(fd, F_SETFL, flags) == -1) {
          throw std::runtime_error("failed to restore test socket flags");
        }
        return received;
      });
  client.connect("127.0.0.1", peer.port(), 2s);
  peer.acceptClient(2s);

  peer.sendSingleWrite(batch, 2s);

  const auto first_packet = client.receivePacket(2s);
  EXPECT_EQ(first_packet.type, rss::protocol::PacketType::Pong);
  EXPECT_EQ(rss::protocol::payloadToString(first_packet), "first");

  const auto second_packet = client.receivePacket(2s);
  EXPECT_EQ(second_packet.type, rss::protocol::PacketType::RoomBroadcast);
  EXPECT_EQ(rss::protocol::payloadToString(second_packet), "second");
  EXPECT_EQ(receive_calls.load(), 1U);
}

TEST(ScenarioClientTest, IncludesServerErrorPayloadInException) {
  auto server = startTestServer();
  rss::tools::ScenarioClient client;
  client.connect("127.0.0.1", server->port(), 2s);

  try {
    client.joinRoom(1, 2s);
    FAIL() << "joinRoom should report the server error";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("login required"),
              std::string::npos);
  }
}

}  // namespace
