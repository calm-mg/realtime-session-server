#include "rss/net/TcpServer.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

std::uint16_t parsePort(const char* value)
{
    const auto port = std::stoi(value);
    if (port <= 0 || port > 65535) {
        throw std::runtime_error("port must be in range 1..65535");
    }
    return static_cast<std::uint16_t>(port);
}

} // namespace

int main(int argc, char** argv)
{
    rss::net::ServerConfig config;
    if (argc >= 2) {
        config.host = argv[1];
    }
    if (argc >= 3) {
        config.port = parsePort(argv[2]);
    }
    if (argc >= 4) {
        config.worker_count = static_cast<std::size_t>(std::stoul(argv[3]));
    } else {
        config.worker_count = std::max(1U, std::thread::hardware_concurrency());
    }

    try {
        rss::net::TcpServer server(config);
        server.run();
    } catch (const std::exception& ex) {
        std::cerr << "server failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
