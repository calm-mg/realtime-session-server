#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>

#include "EmbeddedServer.h"
#include "rss/net/ServerConfig.h"

namespace {

using namespace std::chrono_literals;

TEST(EmbeddedServerTest, StartsOnAnEphemeralLoopbackPortAndStops) {
  rss::net::ServerConfig config;
  config.host = "127.0.0.1";
  config.port = 0;
  config.worker_count = 1;

  rss::tools::EmbeddedServer server(config);
  server.start(2s);
  EXPECT_NE(server.port(), 0);
  EXPECT_EQ(server.snapshot().current_sessions, 0U);
  server.stop();
}

TEST(EmbeddedServerTest, RejectsStartingTwice) {
  rss::net::ServerConfig config;
  config.host = "127.0.0.1";
  config.port = 0;

  rss::tools::EmbeddedServer server(config);
  server.start(2s);
  EXPECT_THROW(server.start(2s), std::logic_error);
}

}  // namespace
