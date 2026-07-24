#pragma once

namespace rss::net {

class CompletionNotifier {
public:
    virtual ~CompletionNotifier() = default;

    virtual void notify() noexcept = 0;
};

} // namespace rss::net
