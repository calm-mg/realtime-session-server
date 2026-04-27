#include "rss/net/EpollEventLoop.h"

#include <cstddef>
#include <stdexcept>
#include <unistd.h>

namespace rss::net {

EpollEventLoop::EpollEventLoop()
    : epoll_fd_(::epoll_create1(EPOLL_CLOEXEC))
{
    if (epoll_fd_ < 0) {
        throw std::runtime_error("epoll_create1 failed");
    }
}

EpollEventLoop::~EpollEventLoop()
{
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
    }
}

void EpollEventLoop::add(int fd, std::uint32_t events)
{
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) < 0) {
        throw std::runtime_error("epoll_ctl add failed");
    }
}

void EpollEventLoop::modify(int fd, std::uint32_t events)
{
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event) < 0) {
        throw std::runtime_error("epoll_ctl modify failed");
    }
}

void EpollEventLoop::remove(int fd)
{
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
}

std::vector<epoll_event> EpollEventLoop::wait(int timeout_ms, int max_events) const
{
    std::vector<epoll_event> events(static_cast<std::size_t>(max_events));
    const auto count = ::epoll_wait(epoll_fd_, events.data(), max_events, timeout_ms);
    if (count < 0) {
        throw std::runtime_error("epoll_wait failed");
    }
    events.resize(static_cast<std::size_t>(count));
    return events;
}

} // namespace rss::net
