#pragma once

#include "rss/net/CompletionNotifier.h"

namespace rss::net {

class EventFdCompletionNotifier final : public CompletionNotifier {
public:
    EventFdCompletionNotifier();
    ~EventFdCompletionNotifier() override;

    EventFdCompletionNotifier(const EventFdCompletionNotifier&) = delete;
    EventFdCompletionNotifier& operator=(const EventFdCompletionNotifier&) = delete;

    [[nodiscard]] int fd() const;
    void notify() noexcept override;
    void drain();

private:
    int fd_{-1};
};

} // namespace rss::net
