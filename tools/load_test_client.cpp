#include "rss/protocol/PacketCodec.h"
#include "rss/protocol/PacketTypes.h"
#include "rss/tools/LatencyStats.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
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

struct ClientResult {
    std::size_t sent{};
    std::size_t failures{};
    std::vector<std::chrono::microseconds> latencies;
};

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

bool waitForPacket(int fd, rss::protocol::PacketCodec& codec, rss::protocol::PacketType expected)
{
    std::uint8_t buffer[4096];
    while (true) {
        const auto n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return false;
        }

        try {
            codec.feed(buffer, static_cast<std::size_t>(n));
            for (const auto& packet : codec.drainPackets()) {
                if (packet.type == expected) {
                    return true;
                }
                if (packet.type == rss::protocol::PacketType::Error) {
                    return false;
                }
            }
        } catch (const rss::protocol::ProtocolError&) {
            return false;
        }
    }
}

std::uint16_t parsePort(const char* value)
{
    const auto port = std::stoi(value);
    if (port <= 0 || port > 65535) {
        throw std::runtime_error("port must be in range 1..65535");
    }
    return static_cast<std::uint16_t>(port);
}

int parsePositiveInt(const char* value, const char* name)
{
    const auto parsed = std::stoi(value);
    if (parsed <= 0) {
        throw std::runtime_error(std::string(name) + " must be positive");
    }
    return parsed;
}

void runClient(const std::string& host,
               std::uint16_t port,
               int client_index,
               int messages,
               ClientResult& result)
{
    using rss::protocol::PacketCodec;
    using rss::protocol::PacketType;

    const auto fd = connectTo(host, port);
    if (fd < 0) {
        ++result.failures;
        return;
    }

    PacketCodec codec;
    const auto login = PacketCodec::encode(PacketType::LoginReq, "load-" + std::to_string(client_index));
    if (!sendAll(fd, login)) {
        ++result.failures;
        ::close(fd);
        return;
    }
    if (!waitForPacket(fd, codec, PacketType::LoginRes)) {
        ++result.failures;
        ::close(fd);
        return;
    }

    const auto ping = PacketCodec::encode(PacketType::Ping, "");
    result.latencies.reserve(static_cast<std::size_t>(messages));
    for (int i = 0; i < messages; ++i) {
        const auto start = std::chrono::steady_clock::now();
        if (!sendAll(fd, ping)) {
            ++result.failures;
            ::close(fd);
            return;
        }
        if (!waitForPacket(fd, codec, PacketType::Pong)) {
            ++result.failures;
            ::close(fd);
            return;
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        result.latencies.push_back(std::chrono::duration_cast<std::chrono::microseconds>(elapsed));
        ++result.sent;
    }

    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
}

std::vector<std::chrono::microseconds> collectLatencies(const std::vector<ClientResult>& results)
{
    std::size_t total = 0;
    for (const auto& result : results) {
        total += result.latencies.size();
    }

    std::vector<std::chrono::microseconds> samples;
    samples.reserve(total);
    for (const auto& result : results) {
        samples.insert(samples.end(), result.latencies.begin(), result.latencies.end());
    }
    return samples;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string host = argc >= 2 ? argv[1] : "127.0.0.1";
    const auto port = argc >= 3 ? parsePort(argv[2]) : static_cast<std::uint16_t>(7777);
    const auto clients = argc >= 4 ? parsePositiveInt(argv[3], "clients") : 100;
    const auto messages = argc >= 5 ? parsePositiveInt(argv[4], "messages") : 100;

    std::vector<ClientResult> results(static_cast<std::size_t>(clients));
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(clients));

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < clients; ++i) {
        threads.emplace_back(runClient, host, port, i, messages, std::ref(results[static_cast<std::size_t>(i)]));
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::size_t sent = 0;
    std::size_t failed = 0;
    for (const auto& result : results) {
        sent += result.sent;
        failed += result.failures;
    }

    const auto samples = collectLatencies(results);
    const auto report = rss::tools::latencyReport(samples);
    const auto messages_per_second = elapsed > 0.0
                                         ? static_cast<std::uint64_t>(static_cast<double>(sent) / elapsed)
                                         : 0;

    std::cout << "clients=" << clients
              << " messages_per_client=" << messages
              << " sent=" << sent
              << " failed_clients=" << failed
              << " elapsed_sec=" << elapsed
              << " approx_msg_per_sec=" << messages_per_second
              << " latency_samples=" << report.sample_count
              << " min_ms=" << report.min_us / 1000.0
              << " p50_ms=" << report.p50_us / 1000.0
              << " p95_ms=" << report.p95_us / 1000.0
              << " p99_ms=" << report.p99_us / 1000.0
              << " max_ms=" << report.max_us / 1000.0
              << '\n';

    return failed == 0 && report.sample_count > 0 ? 0 : 1;
}
