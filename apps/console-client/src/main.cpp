#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "rss/protocol/Packet.h"
#include "rss/protocol/PacketCodec.h"
#include "rss/protocol/PacketTypes.h"
#include "rss/protocol/ProtocolError.h"
#include "rss/protocol/StructuredPayload.h"
#include "rss/protocol/TextValidation.h"

namespace {

int connectTo(const std::string& host, std::uint16_t port) {
  const auto fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    throw std::runtime_error("socket failed");
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
    ::close(fd);
    throw std::runtime_error("invalid host");
  }

  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) <
      0) {
    ::close(fd);
    throw std::runtime_error(std::string("connect failed: ") +
                             std::strerror(errno));
  }

  return fd;
}

void sendAll(int fd, const std::vector<std::uint8_t>& bytes) {
  std::size_t sent = 0;
  while (sent < bytes.size()) {
    const auto n =
        ::send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
    if (n <= 0) {
      throw std::runtime_error(std::string("send failed: ") +
                               std::strerror(errno));
    }
    sent += static_cast<std::size_t>(n);
  }
}

std::vector<std::uint8_t> makePacket(const std::string& line) {
  using rss::protocol::PacketCodec;
  using rss::protocol::PacketType;

  if (line.rfind("/login ", 0) == 0) {
    const auto name =
        rss::protocol::trimAsciiWhitespace(std::string_view(line).substr(7));
    if (name.empty() || name.size() > rss::protocol::kMaxUserNameBytes ||
        !rss::protocol::isValidText(name)) {
      throw std::runtime_error("invalid user name");
    }
    return PacketCodec::encode(PacketType::LoginReq, name);
  }
  if (line.rfind("/create ", 0) == 0) {
    const auto name =
        rss::protocol::trimAsciiWhitespace(std::string_view(line).substr(8));
    if (name.empty() || name.size() > rss::protocol::kMaxRoomNameBytes ||
        !rss::protocol::isValidText(name)) {
      throw std::runtime_error("invalid room name");
    }
    return PacketCodec::encode(PacketType::CreateRoomReq, name);
  }
  if (line.rfind("/join ", 0) == 0) {
    return PacketCodec::encode(PacketType::JoinRoomReq, line.substr(6));
  }
  if (line == "/leave") {
    return PacketCodec::encode(PacketType::LeaveRoomReq, "");
  }
  if (line.rfind("/chat ", 0) == 0) {
    const auto message = std::string_view(line).substr(6);
    if (message.size() > rss::protocol::kMaxChatMessageBytes) {
      throw std::runtime_error("chat message too large");
    }
    if (!rss::protocol::isValidText(message)) {
      throw std::runtime_error("invalid chat message");
    }
    return PacketCodec::encode(PacketType::ChatReq, message);
  }
  if (line.rfind("/pos ", 0) == 0) {
    std::istringstream in(line.substr(5));
    float x{};
    float y{};
    in >> x >> y;
    if (!in) {
      throw std::runtime_error("usage: /pos <x> <y>");
    }
    const auto payload = PacketCodec::encodePosition(x, y);
    return PacketCodec::encode(PacketType::PositionUpdate, payload);
  }
  if (line == "/ping") {
    return PacketCodec::encode(PacketType::Ping, "");
  }
  if (line.size() > rss::protocol::kMaxChatMessageBytes) {
    throw std::runtime_error("chat message too large");
  }
  if (!rss::protocol::isValidText(line)) {
    throw std::runtime_error("invalid chat message");
  }
  return PacketCodec::encode(PacketType::ChatReq, line);
}

bool isStructured(rss::protocol::PacketType type) {
  using rss::protocol::PacketType;
  return type == PacketType::LoginRes || type == PacketType::CreateRoomRes ||
         type == PacketType::JoinRoomRes || type == PacketType::LeaveRoomRes ||
         type == PacketType::RoomBroadcast;
}

void printStructured(const rss::protocol::StructuredPayload& payload) {
  bool needs_separator = false;
  if (const auto status = payload.status(); status.has_value()) {
    std::cout << *status;
    needs_separator = true;
  }
  for (const auto& [key, value] : payload.fields()) {
    std::cout << (needs_separator ? "|" : "") << key << '=' << value;
    needs_separator = true;
  }
}

void printPacket(const rss::protocol::Packet& packet) {
  const auto payload = rss::protocol::payloadToString(packet);
  try {
    if (isStructured(packet.type)) {
      const auto structured = rss::protocol::StructuredPayload::parse(payload);
      std::cout << '[' << rss::protocol::toString(packet.type) << "] ";
      printStructured(structured);
    } else if ((packet.type == rss::protocol::PacketType::Error ||
                packet.type == rss::protocol::PacketType::Pong) &&
               rss::protocol::isValidText(payload)) {
      std::cout << '[' << rss::protocol::toString(packet.type) << "] "
                << payload;
    } else {
      throw rss::protocol::ProtocolError("invalid packet text");
    }
    std::cout << '\n';
  } catch (const rss::protocol::ProtocolError&) {
    std::cout << "[PROTOCOL_ERROR] invalid structured payload\n";
  }
}

void readLoop(int fd, std::atomic_bool& running) {
  rss::protocol::PacketCodec codec;
  std::uint8_t buffer[4096];
  while (running.load()) {
    const auto n = ::recv(fd, buffer, sizeof(buffer), 0);
    if (n <= 0) {
      running.store(false);
      break;
    }

    codec.feed(buffer, static_cast<std::size_t>(n));
    for (const auto& packet : codec.drainPackets()) {
      printPacket(packet);
    }
  }
}

std::uint16_t parsePort(const char* value) {
  const auto port = std::stoi(value);
  if (port <= 0 || port > 65535) {
    throw std::runtime_error("port must be in range 1..65535");
  }
  return static_cast<std::uint16_t>(port);
}

}  // namespace

int main(int argc, char** argv) {
  const std::string host = argc >= 2 ? argv[1] : "127.0.0.1";
  const auto port =
      argc >= 3 ? parsePort(argv[2]) : static_cast<std::uint16_t>(7777);

  try {
    const auto fd = connectTo(host, port);
    std::atomic_bool running{true};
    std::thread reader([fd, &running] { readLoop(fd, running); });

    std::cout << "connected. commands: /login, /create, /join, /leave, /chat, "
                 "/pos, /ping, /quit\n";
    std::string line;
    while (running.load() && std::getline(std::cin, line)) {
      if (line == "/quit") {
        break;
      }
      std::vector<std::uint8_t> packet;
      try {
        packet = makePacket(line);
      } catch (const std::runtime_error& ex) {
        std::cerr << ex.what() << '\n';
        continue;
      }
      sendAll(fd, packet);
    }

    running.store(false);
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
    if (reader.joinable()) {
      reader.join();
    }
  } catch (const std::exception& ex) {
    std::cerr << "client failed: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
