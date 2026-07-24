#include <gtest/gtest.h>
#include <poll.h>

#include "rss/net/EventFdCompletionNotifier.h"

namespace {

bool isReadable(int fd) {
  pollfd entry{};
  entry.fd = fd;
  entry.events = POLLIN;
  const auto ready = ::poll(&entry, 1, 0);
  EXPECT_GE(ready, 0);
  return ready == 1 && (entry.revents & POLLIN) != 0;
}

TEST(EventFdCompletionNotifierTest, BecomesReadableUntilDrained) {
  rss::net::EventFdCompletionNotifier notifier;

  EXPECT_FALSE(isReadable(notifier.fd()));
  notifier.notify();
  EXPECT_TRUE(isReadable(notifier.fd()));
  notifier.drain();
  EXPECT_FALSE(isReadable(notifier.fd()));
}

}  // namespace
