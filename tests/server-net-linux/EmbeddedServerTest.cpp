#include <gtest/gtest.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "EmbeddedServer.h"
#include "rss/net/ServerConfig.h"
#include "rss/net/TcpServer.h"

namespace {

using namespace std::chrono_literals;

std::string captureTcpServerStartupDiagnostic() {
  rss::net::ServerConfig config;
  config.host = "127.0.0.1";
  config.port = 0;
  config.worker_count = 1;

  rss::net::TcpServer server(config);
  std::exception_ptr failure;
  testing::internal::CaptureStdout();
  std::thread thread([&] {
    try {
      server.run();
    } catch (...) {
      failure = std::current_exception();
    }
  });

  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (server.boundPort() == 0 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  server.stop();
  thread.join();
  std::cout.flush();
  auto output = testing::internal::GetCapturedStdout();
  if (failure != nullptr) {
    std::rethrow_exception(failure);
  }
  return output;
}

TEST(EmbeddedServerTest, TcpServerEmitsStartupDiagnosticByDefault) {
  const auto output = captureTcpServerStartupDiagnostic();

  EXPECT_NE(output.find("\"level\":\"info\""), std::string::npos);
  EXPECT_NE(output.find("\"event\":\"server_started\""), std::string::npos);
  EXPECT_NE(output.find("\"host\":\"127.0.0.1\""), std::string::npos);
  EXPECT_NE(output.find("\"worker_count\":1"), std::string::npos);
}

TEST(EmbeddedServerTest, DoesNotEmitStartupDiagnostic) {
  rss::net::ServerConfig config;
  config.host = "127.0.0.1";
  config.port = 0;
  config.worker_count = 1;

  testing::internal::CaptureStdout();
  {
    rss::tools::EmbeddedServer server(config);
    server.start(2s);
    server.stop();
  }
  std::cout.flush();
  const auto output = testing::internal::GetCapturedStdout();

  EXPECT_TRUE(output.empty());
}

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
