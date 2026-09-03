#include "ScenarioClient.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "rss/protocol/ProtocolError.h"
#include "rss/protocol/StructuredPayload.h"

namespace rss::tools {
namespace {

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

std::runtime_error systemError(std::string_view operation, int error) {
  return std::runtime_error(std::string(operation) + ": " +
                            std::strerror(error));
}

int remainingMilliseconds(Deadline deadline) {
  const auto now = Clock::now();
  if (now >= deadline) {
    return 0;
  }

  const auto remaining = deadline - now;
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
  const auto rounded =
      milliseconds + (milliseconds < remaining ? std::chrono::milliseconds(1)
                                               : std::chrono::milliseconds(0));
  return static_cast<int>(std::min<std::int64_t>(
      rounded.count(), static_cast<std::int64_t>(INT_MAX)));
}

bool waitForSocketReady(int fd, std::int16_t events, Deadline deadline,
                        std::string_view operation) {
  while (true) {
    pollfd descriptor{fd, events, 0};
    const auto result = ::poll(&descriptor, 1, remainingMilliseconds(deadline));
    if (result > 0) {
      if ((descriptor.revents & POLLNVAL) != 0) {
        throw std::runtime_error(std::string(operation) + ": invalid socket");
      }
      return true;
    }
    if (result == 0) {
      return false;
    }
    if (errno != EINTR) {
      throw systemError(operation, errno);
    }
  }
}

void waitForSocket(int fd, std::int16_t events, Deadline deadline,
                   std::string_view operation) {
  if (!waitForSocketReady(fd, events, deadline, operation)) {
    throw std::runtime_error(std::string(operation) + " timed out");
  }
}

void ensureBeforeDeadline(Deadline deadline, std::string_view operation) {
  if (Clock::now() >= deadline) {
    throw std::runtime_error(std::string(operation) + " timed out");
  }
}

std::chrono::milliseconds remainingTimeout(Deadline deadline) {
  return std::chrono::milliseconds(remainingMilliseconds(deadline));
}

std::ptrdiff_t receiveFromSocket(int fd, std::uint8_t* buffer,
                                 std::size_t capacity) {
  return ::recv(fd, buffer, capacity, 0);
}

}  // namespace

ScenarioClient::ScenarioClient()
    : receive_operation_(defaultReceiveOperation()) {}

ScenarioClient::ScenarioClient(ReceiveOperation receive_operation)
    : receive_operation_(std::move(receive_operation)) {
  if (!receive_operation_) {
    throw std::invalid_argument("receive operation must not be empty");
  }
}

ScenarioClient::~ScenarioClient() { close(); }

ScenarioClient::ScenarioClient(ScenarioClient&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)),
      codec_(std::move(other.codec_)),
      receive_operation_(defaultReceiveOperation()) {
  receive_operation_.swap(other.receive_operation_);
  other.codec_ = {};
}

ScenarioClient& ScenarioClient::operator=(ScenarioClient&& other) noexcept {
  if (this != &other) {
    close();
    fd_ = std::exchange(other.fd_, -1);
    codec_ = std::move(other.codec_);
    receive_operation_ = defaultReceiveOperation();
    receive_operation_.swap(other.receive_operation_);
    other.codec_ = {};
  }
  return *this;
}

ScenarioClient::ReceiveOperation
ScenarioClient::defaultReceiveOperation() noexcept {
  return receiveFromSocket;
}

void ScenarioClient::connect(std::string_view host, std::uint16_t port,
                             std::chrono::milliseconds timeout) {
  if (fd_ != -1) {
    throw std::logic_error("scenario client is already connected");
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  const std::string host_text(host);
  const auto parse_result =
      ::inet_pton(AF_INET, host_text.c_str(), &address.sin_addr);
  if (parse_result != 1) {
    throw std::invalid_argument("scenario client host must be an IPv4 address");
  }

  const auto deadline = Clock::now() + timeout;
  while (fd_ == -1) {
    ensureBeforeDeadline(deadline, "connect");
    fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ == -1 && errno != EINTR) {
      throw systemError("socket", errno);
    }
  }

  try {
    while (true) {
      ensureBeforeDeadline(deadline, "connect");
      const auto result = ::connect(fd_, reinterpret_cast<sockaddr*>(&address),
                                    sizeof(address));
      if (result == 0 || (result == -1 && errno == EISCONN)) {
        return;
      }
      if (errno == EINTR) {
        continue;
      }
      if (errno == EINPROGRESS || errno == EALREADY) {
        break;
      }
      throw systemError("connect", errno);
    }

    waitForSocket(fd_, POLLOUT, deadline, "connect");

    int socket_error{};
    socklen_t error_size = sizeof(socket_error);
    while (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &socket_error,
                        &error_size) == -1) {
      if (errno != EINTR) {
        throw systemError("getsockopt(SO_ERROR)", errno);
      }
      ensureBeforeDeadline(deadline, "connect");
    }
    if (socket_error != 0) {
      throw systemError("connect", socket_error);
    }
  } catch (...) {
    close();
    throw;
  }
}

void ScenarioClient::setReceiveBufferBytes(int bytes) {
  if (fd_ == -1) {
    throw std::logic_error("scenario client is not connected");
  }
  if (bytes <= 0) {
    throw std::invalid_argument("receive buffer size must be positive");
  }
  if (::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) == -1) {
    throw systemError("setsockopt(SO_RCVBUF)", errno);
  }
}

void ScenarioClient::login(std::string_view name,
                           std::chrono::milliseconds timeout) {
  sendPacket(rss::protocol::PacketType::LoginReq, name, timeout);
  static_cast<void>(waitFor(rss::protocol::PacketType::LoginRes, timeout));
}

std::uint32_t ScenarioClient::createRoom(std::string_view name,
                                         std::chrono::milliseconds timeout) {
  sendPacket(rss::protocol::PacketType::CreateRoomReq, name, timeout);
  const auto response =
      waitFor(rss::protocol::PacketType::CreateRoomRes, timeout);
  const auto payload = rss::protocol::payloadToString(response);
  try {
    const auto structured = rss::protocol::StructuredPayload::parse(payload);
    if (structured.status() != "OK") {
      throw std::runtime_error("create room response is not successful");
    }

    const auto room_id_value = structured.requireField("room_id");
    std::uint32_t room_id{};
    const auto* begin = room_id_value.data();
    const auto* end = begin + room_id_value.size();
    const auto [ptr, error] = std::from_chars(begin, end, room_id);
    if (error != std::errc{} || ptr != end || begin == end) {
      throw std::runtime_error("create room response has invalid room_id");
    }
    return room_id;
  } catch (const rss::protocol::ProtocolError&) {
    throw std::runtime_error("create room response is malformed");
  }
}

void ScenarioClient::joinRoom(std::uint32_t room_id,
                              std::chrono::milliseconds timeout) {
  const auto payload = std::to_string(room_id);
  sendPacket(rss::protocol::PacketType::JoinRoomReq, payload, timeout);
  static_cast<void>(waitFor(rss::protocol::PacketType::JoinRoomRes, timeout));
}

void ScenarioClient::sendChat(std::string_view payload,
                              std::chrono::milliseconds timeout) {
  sendPacket(rss::protocol::PacketType::ChatReq, payload, timeout);
}

rss::protocol::Packet ScenarioClient::receivePacket(
    std::chrono::milliseconds timeout) {
  auto packet = tryReceivePacket(timeout);
  if (!packet.has_value()) {
    throw std::runtime_error("receive timed out");
  }
  return std::move(*packet);
}

std::optional<rss::protocol::Packet> ScenarioClient::tryReceivePacket(
    std::chrono::milliseconds timeout) {
  if (fd_ == -1) {
    throw std::logic_error("scenario client is not connected");
  }

  if (auto packet = codec_.peekPacket()) {
    codec_.consumePacket();
    return std::move(*packet);
  }

  const auto deadline = Clock::now() + timeout;
  std::array<std::uint8_t, rss::protocol::kMaxPacketSize> buffer{};
  while (true) {
    if (Clock::now() >= deadline) {
      return std::nullopt;
    }
    const auto received = receive_operation_(fd_, buffer.data(), buffer.size());
    if (received > 0) {
      codec_.feed(buffer.data(), static_cast<std::size_t>(received));
      if (auto packet = codec_.peekPacket()) {
        codec_.consumePacket();
        return std::move(*packet);
      }
      continue;
    }
    if (received == 0) {
      throw std::runtime_error("connection closed while receiving");
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      if (!waitForSocketReady(fd_, POLLIN, deadline, "receive")) {
        return std::nullopt;
      }
      continue;
    }
    throw systemError("recv", errno);
  }
}

void ScenarioClient::close() noexcept {
  if (fd_ != -1) {
    static_cast<void>(::close(fd_));
    fd_ = -1;
  }
  codec_ = {};
}

void ScenarioClient::sendPacket(rss::protocol::PacketType type,
                                std::string_view payload,
                                std::chrono::milliseconds timeout) {
  if (fd_ == -1) {
    throw std::logic_error("scenario client is not connected");
  }

  const auto bytes = rss::protocol::PacketCodec::encode(type, payload);
  const auto deadline = Clock::now() + timeout;
  std::size_t offset{};
  while (offset < bytes.size()) {
    ensureBeforeDeadline(deadline, "send");
    const auto sent =
        ::send(fd_, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
    if (sent > 0) {
      offset += static_cast<std::size_t>(sent);
      continue;
    }
    if (sent == 0) {
      throw std::runtime_error("connection closed while sending");
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      waitForSocket(fd_, POLLOUT, deadline, "send");
      continue;
    }
    throw systemError("send", errno);
  }
}

rss::protocol::Packet ScenarioClient::waitFor(
    rss::protocol::PacketType expected, std::chrono::milliseconds timeout) {
  const auto deadline = Clock::now() + timeout;
  while (true) {
    const auto packet = receivePacket(remainingTimeout(deadline));
    if (packet.type == rss::protocol::PacketType::Error) {
      throw std::runtime_error("server error: " +
                               rss::protocol::payloadToString(packet));
    }
    if (packet.type == expected) {
      return packet;
    }
  }
}

}  // namespace rss::tools
