#pragma once

#include <sys/epoll.h>

#include <cstdint>
#include <vector>

namespace rss::net {

class EpollEventLoop {
public:
    EpollEventLoop();
    ~EpollEventLoop();

    EpollEventLoop(const EpollEventLoop&) = delete;
    EpollEventLoop& operator=(const EpollEventLoop&) = delete;

    void add(int fd, std::uint32_t events);
    void modify(int fd, std::uint32_t events);
    void remove(int fd);
    [[nodiscard]] std::vector<epoll_event> wait(int timeout_ms, int max_events) const;

private:
    int epoll_fd_{-1};
};

} // namespace rss::net
