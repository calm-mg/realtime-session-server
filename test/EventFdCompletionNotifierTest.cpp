#include "TestSupport.h"

#include "rss/net/EventFdCompletionNotifier.h"

#include <poll.h>

namespace {

bool isReadable(int fd)
{
    pollfd entry{};
    entry.fd = fd;
    entry.events = POLLIN;
    const auto ready = ::poll(&entry, 1, 0);
    RSS_EXPECT(ready >= 0);
    return ready == 1 && (entry.revents & POLLIN) != 0;
}

} // namespace

int main()
{
    rss::net::EventFdCompletionNotifier notifier;

    RSS_EXPECT(!isReadable(notifier.fd()));
    notifier.notify();
    RSS_EXPECT(isReadable(notifier.fd()));
    notifier.drain();
    RSS_EXPECT(!isReadable(notifier.fd()));

    testPassed("EventFdCompletionNotifierTest");
    return 0;
}
