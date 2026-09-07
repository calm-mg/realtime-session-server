#include "rss/net/ClientVersionNegotiation.h"

#include <poll.h>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>

#include "rss/net/ClientIoError.h"
#include "rss/protocol/PacketCodec.h"
#include "rss/protocol/ProtocolVersion.h"

namespace rss::net {
namespace {

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

void ensureBeforeDeadline(Deadline deadline) {
  if (Clock::now() >= deadline) {
    throw ClientIoError(ClientIoFailure::Timeout,
                        "protocol version negotiation timed out");
  }
}

ClientIoError socketError() {
  return ClientIoError(
      ClientIoFailure::SocketError,
      std::string("protocol version negotiation: ") + std::strerror(errno));
}

void waitForSocket(int fd, std::int16_t events, Deadline deadline) {
  while (true) {
    const auto now = Clock::now();
    if (now >= deadline) {
      throw ClientIoError(ClientIoFailure::Timeout,
                          "protocol version negotiation timed out");
    }
    const auto remaining =
        std::chrono::ceil<std::chrono::milliseconds>(deadline - now);
    pollfd descriptor{fd, events, 0};
    const auto ready =
        ::poll(&descriptor, 1, static_cast<int>(remaining.count()));
    if (ready > 0) {
      if ((descriptor.revents & POLLNVAL) != 0) {
        throw ClientIoError(ClientIoFailure::SocketError,
                            "protocol version negotiation: invalid socket");
      }
      return;
    }
    if (ready < 0 && errno != EINTR) {
      throw socketError();
    }
  }
}

void sendRequest(int fd, std::span<const std::uint8_t> bytes,
                 Deadline deadline) {
  while (!bytes.empty()) {
    ensureBeforeDeadline(deadline);
    const auto sent =
        ::send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
    if (sent > 0) {
      bytes = bytes.subspan(static_cast<std::size_t>(sent));
    } else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      waitForSocket(fd, POLLOUT, deadline);
    } else if (sent == 0) {
      throw ClientIoError(ClientIoFailure::PeerClosed,
                          "connection closed during version negotiation");
    } else if (errno != EINTR) {
      throw socketError();
    }
  }
}

void receiveExactly(int fd, std::span<std::uint8_t> bytes, Deadline deadline) {
  while (!bytes.empty()) {
    ensureBeforeDeadline(deadline);
    const auto received = ::recv(fd, bytes.data(), bytes.size(), MSG_DONTWAIT);
    if (received > 0) {
      bytes = bytes.subspan(static_cast<std::size_t>(received));
    } else if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      waitForSocket(fd, POLLIN, deadline);
    } else if (received == 0) {
      throw ClientIoError(ClientIoFailure::PeerClosed,
                          "connection closed during version negotiation");
    } else if (errno != EINTR) {
      throw socketError();
    }
  }
}

}  // namespace

std::uint16_t negotiateClientVersion(int fd, Deadline deadline) {
  using rss::protocol::decodeVersionResponse;
  using rss::protocol::encodeVersionRequest;
  using rss::protocol::kMaxPacketSize;
  using rss::protocol::kMaxProtocolVersion;
  using rss::protocol::kMinProtocolVersion;
  using rss::protocol::kPacketHeaderSize;
  using rss::protocol::kVersionNegotiationTimeout;
  using rss::protocol::PacketCodec;
  using rss::protocol::PacketType;
  using rss::protocol::payloadToString;
  using rss::protocol::ProtocolError;
  deadline = std::min(deadline, Clock::now() + kVersionNegotiationTimeout);
  const auto request = PacketCodec::encode(
      PacketType::VersionReq,
      encodeVersionRequest({kMinProtocolVersion, kMaxProtocolVersion}));
  sendRequest(fd, request, deadline);

  // 한 frame만 읽어 함께 도착한 업무 packet을 다음 수신에 보존한다.
  std::array<std::uint8_t, kMaxPacketSize> bytes{};
  receiveExactly(fd, std::span(bytes).first(kPacketHeaderSize), deadline);
  const auto size =
      static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8U) |
                                 static_cast<std::uint16_t>(bytes[1]));
  if (size < kPacketHeaderSize || size > kMaxPacketSize) {
    throw ProtocolError("invalid version response packet size");
  }
  receiveExactly(
      fd, std::span(bytes).subspan(kPacketHeaderSize, size - kPacketHeaderSize),
      deadline);
  PacketCodec codec;
  codec.feed(bytes.data(), size);
  const auto packet = codec.peekPacket();
  if (!packet || packet->type != PacketType::VersionRes) {
    throw ProtocolError("expected protocol version response");
  }
  const auto version = decodeVersionResponse(payloadToString(*packet));
  if (version < kMinProtocolVersion || version > kMaxProtocolVersion) {
    throw ProtocolError("server selected unsupported protocol version");
  }
  ensureBeforeDeadline(deadline);
  return version;
}

}  // namespace rss::net
