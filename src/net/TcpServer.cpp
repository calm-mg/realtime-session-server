#include "rss/net/TcpServer.h"

#include "rss/protocol/PacketCodec.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace rss::net {
namespace {

int setNonBlocking(int fd)
{
    const auto flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool wouldBlock()
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

std::runtime_error systemError(const char* message)
{
    return std::runtime_error(std::string(message) + ": " + std::strerror(errno));
}

sockaddr_in makeAddress(const std::string& host, std::uint16_t port)
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        throw std::runtime_error("invalid IPv4 bind address: " + host);
    }
    return address;
}

} // namespace

TcpServer::TcpServer(ServerConfig config)
    : config_(std::move(config))
    , router_(room_service_)
    , workers_(inbox_, outbox_, router_, &outbound_wakeup_)
{
}

TcpServer::~TcpServer()
{
    stop();
    for (auto& [fd, _] : sessions_by_fd_) {
        ::close(fd);
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
    }
}

void TcpServer::run()
{
    openListener();
    workers_.start(config_.worker_count);
    running_ = true;

    std::cout << "rss_server listening on " << config_.host << ':' << config_.port
              << " with " << config_.worker_count << " workers\n";

    while (running_) {
        auto events = event_loop_.wait(1000, config_.max_events);
        for (const auto& event : events) {
            const auto fd = event.data.fd;
            if (fd == outbound_wakeup_.fd()) {
                outbound_wakeup_.drain();
                drainOutbound();
                continue;
            }

            if (fd == listen_fd_) {
                acceptLoop();
                continue;
            }

            if ((event.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U) {
                disconnect(fd);
                continue;
            }

            if ((event.events & EPOLLIN) != 0U) {
                readSession(fd);
            }

            if (sessions_by_fd_.contains(fd) && (event.events & EPOLLOUT) != 0U) {
                flushSession(fd);
            }
        }

        drainOutbound();
        expireIdleSessions();
    }
}

void TcpServer::stop()
{
    running_ = false;
    workers_.stop();
}

void TcpServer::openListener()
{
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        throw systemError("socket failed");
    }

    const int yes = 1;
    if (::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        throw systemError("setsockopt SO_REUSEADDR failed");
    }

    if (setNonBlocking(listen_fd_) < 0) {
        throw systemError("failed to set listener non-blocking");
    }

    auto address = makeAddress(config_.host, config_.port);
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw systemError("bind failed");
    }

    if (::listen(listen_fd_, config_.backlog) < 0) {
        throw systemError("listen failed");
    }

    event_loop_.add(listen_fd_, EPOLLIN);
    event_loop_.add(outbound_wakeup_.fd(), EPOLLIN);
}

void TcpServer::acceptLoop()
{
    while (true) {
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        const auto fd = ::accept4(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len, SOCK_CLOEXEC);
        if (fd < 0) {
            if (wouldBlock()) {
                return;
            }
            throw systemError("accept4 failed");
        }

        if (setNonBlocking(fd) < 0) {
            ::close(fd);
            continue;
        }

        const auto session_id = next_session_id_++;
        auto session = std::make_unique<Session>(fd, session_id);
        fd_by_session_[session_id] = fd;
        sessions_by_fd_[fd] = std::move(session);
        event_loop_.add(fd, EPOLLIN | EPOLLRDHUP);
    }
}

void TcpServer::readSession(int fd)
{
    auto it = sessions_by_fd_.find(fd);
    if (it == sessions_by_fd_.end()) {
        return;
    }

    auto& session = *it->second;
    std::uint8_t buffer[4096];

    while (true) {
        const auto n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n == 0) {
            disconnect(fd);
            return;
        }
        if (n < 0) {
            if (wouldBlock()) {
                break;
            }
            disconnect(fd);
            return;
        }

        try {
            session.touch();
            session.codec().feed(buffer, static_cast<std::size_t>(n));
            for (auto& packet : session.codec().drainPackets()) {
                inbox_.push(service::SessionEvent{service::SessionEventKind::Packet, session.id(), std::move(packet)});
            }
        } catch (const protocol::ProtocolError&) {
            disconnect(fd);
            return;
        }
    }
}

void TcpServer::flushSession(int fd)
{
    auto it = sessions_by_fd_.find(fd);
    if (it == sessions_by_fd_.end()) {
        return;
    }

    auto& session = *it->second;
    while (session.hasPendingWrite()) {
        auto& write = session.currentWrite();
        const auto* data = write.bytes.data() + write.offset;
        const auto remaining = write.bytes.size() - write.offset;
        const auto n = ::send(fd, data, remaining, MSG_NOSIGNAL);

        if (n == 0) {
            disconnect(fd);
            return;
        }

        if (n < 0) {
            if (wouldBlock()) {
                updateInterest(session);
                return;
            }
            disconnect(fd);
            return;
        }

        session.consumeWrite(static_cast<std::size_t>(n));
    }

    updateInterest(session);
}

void TcpServer::disconnect(int fd)
{
    auto it = sessions_by_fd_.find(fd);
    if (it == sessions_by_fd_.end()) {
        return;
    }

    const auto session_id = it->second->id();
    inbox_.push(service::SessionEvent{service::SessionEventKind::Disconnected, session_id, {}});
    fd_by_session_.erase(session_id);
    event_loop_.remove(fd);
    ::close(fd);
    sessions_by_fd_.erase(it);
}

void TcpServer::drainOutbound()
{
    while (auto message = outbox_.tryPop()) {
        const auto fd_it = fd_by_session_.find(message->session_id);
        if (fd_it == fd_by_session_.end()) {
            continue;
        }

        auto session_it = sessions_by_fd_.find(fd_it->second);
        if (session_it == sessions_by_fd_.end()) {
            continue;
        }

        session_it->second->enqueue(std::move(message->bytes));
        updateInterest(*session_it->second);
    }
}

void TcpServer::updateInterest(Session& session)
{
    auto events = static_cast<std::uint32_t>(EPOLLIN | EPOLLRDHUP);
    if (session.hasPendingWrite()) {
        events |= EPOLLOUT;
    }
    event_loop_.modify(session.fd(), events);
}

void TcpServer::expireIdleSessions()
{
    const auto now = std::chrono::steady_clock::now();
    std::vector<int> expired;
    for (const auto& [fd, session] : sessions_by_fd_) {
        if (now - session->lastSeen() > config_.idle_timeout) {
            expired.push_back(fd);
        }
    }
    for (const auto fd : expired) {
        disconnect(fd);
    }
}

} // namespace rss::net
