#include "rss/protocol/PacketCodec.h"
#include "rss/protocol/PacketTypes.h"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

int connectTo(const std::string& host, std::uint16_t port)
{
    const auto fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        ::close(fd);
        return -1;
    }

    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

bool sendAll(int fd, const std::vector<std::uint8_t>& bytes)
{
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const auto n = ::send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

std::uint16_t parsePort(const char* value)
{
    const auto port = std::stoi(value);
    if (port <= 0 || port > 65535) {
        throw std::runtime_error("port must be in range 1..65535");
    }
    return static_cast<std::uint16_t>(port);
}

void runClient(const std::string& host,
               std::uint16_t port,
               int client_index,
               int messages,
               std::atomic_int& ok,
               std::atomic_int& failed)
{
    using rss::protocol::PacketCodec;
    using rss::protocol::PacketType;

    const auto fd = connectTo(host, port);
    if (fd < 0) {
        failed.fetch_add(1);
        return;
    }

    const auto login = PacketCodec::encode(PacketType::LoginReq, "load-" + std::to_string(client_index));
    if (!sendAll(fd, login)) {
        failed.fetch_add(1);
        ::close(fd);
        return;
    }

    const auto ping = PacketCodec::encode(PacketType::Ping, "");
    for (int i = 0; i < messages; ++i) {
        if (!sendAll(fd, ping)) {
            failed.fetch_add(1);
            ::close(fd);
            return;
        }
        ok.fetch_add(1);
    }

    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
}

} // namespace

int main(int argc, char** argv)
{
    const std::string host = argc >= 2 ? argv[1] : "127.0.0.1";
    const auto port = argc >= 3 ? parsePort(argv[2]) : static_cast<std::uint16_t>(7777);
    const auto clients = argc >= 4 ? std::stoi(argv[3]) : 100;
    const auto messages = argc >= 5 ? std::stoi(argv[4]) : 100;

    std::atomic_int ok{0};
    std::atomic_int failed{0};
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(clients));

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < clients; ++i) {
        threads.emplace_back(runClient, host, port, i, messages, std::ref(ok), std::ref(failed));
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::cout << "clients=" << clients
              << " messages_per_client=" << messages
              << " sent=" << ok.load()
              << " failed_clients=" << failed.load()
              << " elapsed_sec=" << elapsed
              << " approx_msg_per_sec=" << static_cast<int>(ok.load() / elapsed)
              << '\n';

    return failed.load() == 0 ? 0 : 1;
}
