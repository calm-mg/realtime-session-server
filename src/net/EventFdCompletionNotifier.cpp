#include "rss/net/EventFdCompletionNotifier.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/eventfd.h>
#include <unistd.h>

namespace rss::net {
namespace {

std::runtime_error systemError(const char* message)
{
    return std::runtime_error(std::string(message) + ": " + std::strerror(errno));
}

bool wouldBlock()
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

} // namespace

EventFdCompletionNotifier::EventFdCompletionNotifier()
    : fd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))
{
    if (fd_ < 0) {
        throw systemError("eventfd failed");
    }
}

EventFdCompletionNotifier::~EventFdCompletionNotifier()
{
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

int EventFdCompletionNotifier::fd() const
{
    return fd_;
}

void EventFdCompletionNotifier::notify() noexcept
{
    const std::uint64_t value = 1;
    while (true) {
        const auto n = ::write(fd_, &value, sizeof(value));
        if (n == static_cast<ssize_t>(sizeof(value))) {
            return;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return;
    }
}

void EventFdCompletionNotifier::drain()
{
    while (true) {
        std::uint64_t value{};
        const auto n = ::read(fd_, &value, sizeof(value));
        if (n == static_cast<ssize_t>(sizeof(value))) {
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && wouldBlock()) {
            return;
        }
        if (n == 0) {
            return;
        }
        throw systemError("eventfd read failed");
    }
}

} // namespace rss::net
