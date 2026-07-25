#include <arpa/inet.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "rss/net/ServerConfig.h"
#include "rss/net/TcpServer.h"
#include "rss/net/detail/AcceptBatchLimiter.h"
#include "rss/protocol/PacketCodec.h"
#include "rss/service/SessionEventHandler.h"

namespace {

using namespace std::chrono_literals;
using rss::protocol::PacketCodec;
using rss::protocol::PacketType;
using rss::service::OutboundMessage;
using rss::service::SessionEvent;
using rss::service::SessionEventKind;

TEST(AcceptBatchLimiterTest, AllowsOnlyConfiguredNumberOfAccepts) {
  rss::net::detail::AcceptBatchLimiter limiter(2);

  EXPECT_TRUE(limiter.tryAcquire(false));
  EXPECT_TRUE(limiter.tryAcquire(false));
  EXPECT_FALSE(limiter.tryAcquire(false));
}

TEST(AcceptBatchLimiterTest, StopsBeforeRemainingBudgetOnStopRequest) {
  rss::net::detail::AcceptBatchLimiter limiter(3);

  EXPECT_TRUE(limiter.tryAcquire(false));
  EXPECT_FALSE(limiter.tryAcquire(true));
}

template <typename Predicate>
bool waitUntil(Predicate&& predicate,
               std::chrono::steady_clock::duration timeout = 1s) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return predicate();
}

template <typename Predicate>
bool remainsTrueFor(Predicate&& predicate,
                    std::chrono::steady_clock::duration duration) {
  const auto deadline = std::chrono::steady_clock::now() + duration;
  while (std::chrono::steady_clock::now() < deadline) {
    if (!predicate()) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  return predicate();
}

class ClientSocket {
 public:
  ClientSocket() = default;
  explicit ClientSocket(int fd) : fd_(fd) {}

  ~ClientSocket() { reset(); }

  ClientSocket(const ClientSocket&) = delete;
  ClientSocket& operator=(const ClientSocket&) = delete;

  ClientSocket(ClientSocket&& other) noexcept
      : fd_(std::exchange(other.fd_, -1)) {}

  ClientSocket& operator=(ClientSocket&& other) noexcept {
    if (this != &other) {
      reset();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }

  [[nodiscard]] bool valid() const { return fd_ >= 0; }
  [[nodiscard]] int get() const { return fd_; }

  bool shutdownWrite() { return ::shutdown(fd_, SHUT_WR) == 0; }

  void reset() noexcept {
    if (fd_ < 0) {
      return;
    }
    ::shutdown(fd_, SHUT_RDWR);
    ::close(fd_);
    fd_ = -1;
  }

 private:
  int fd_{-1};
};

ClientSocket connectClient(std::uint16_t port,
                           std::chrono::steady_clock::duration timeout = 1s,
                           int receive_buffer_size = 0) {
  const auto fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return {};
  }

  if (receive_buffer_size > 0 &&
      ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer_size,
                   sizeof(receive_buffer_size)) < 0) {
    ::close(fd);
    return {};
  }

  const auto flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    ::close(fd);
    return {};
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const auto connect_result =
      ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  if (connect_result < 0 && errno != EINPROGRESS) {
    ::close(fd);
    return {};
  }

  if (connect_result < 0) {
    pollfd entry{};
    entry.fd = fd;
    entry.events = POLLOUT;
    const auto timeout_ms = std::max<std::int64_t>(
        1,
        std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    int ready = -1;
    do {
      ready = ::poll(&entry, 1, static_cast<int>(timeout_ms));
    } while (ready < 0 && errno == EINTR);

    int socket_error = 0;
    socklen_t socket_error_size = sizeof(socket_error);
    if (ready != 1 ||
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error,
                     &socket_error_size) < 0 ||
        socket_error != 0) {
      ::close(fd);
      return {};
    }
  }

  if (::fcntl(fd, F_SETFL, flags) < 0) {
    ::close(fd);
    return {};
  }
  return ClientSocket(fd);
}

bool sendSingleWrite(int fd, const std::vector<std::uint8_t>& bytes) {
  ssize_t sent = -1;
  do {
    sent = ::send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
  } while (sent < 0 && errno == EINTR);
  return sent == static_cast<ssize_t>(bytes.size());
}

std::vector<std::uint8_t> encodePingBatch(
    const std::vector<std::string>& payloads) {
  std::vector<std::uint8_t> bytes;
  for (const auto& payload : payloads) {
    auto frame = PacketCodec::encode(PacketType::Ping, payload);
    bytes.insert(bytes.end(), frame.begin(), frame.end());
  }
  return bytes;
}

std::vector<rss::protocol::Packet> receivePackets(
    int fd, std::size_t expected_count,
    std::chrono::steady_clock::duration timeout = 2s) {
  PacketCodec codec;
  std::vector<rss::protocol::Packet> packets;
  const auto deadline = std::chrono::steady_clock::now() + timeout;

  while (packets.size() < expected_count &&
         std::chrono::steady_clock::now() < deadline) {
    pollfd entry{};
    entry.fd = fd;
    entry.events = POLLIN;
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
    const auto poll_timeout =
        static_cast<int>(std::max<std::int64_t>(1, remaining.count()));
    const auto ready = ::poll(&entry, 1, poll_timeout);
    if (ready < 0 && errno == EINTR) {
      continue;
    }
    if (ready <= 0) {
      break;
    }

    std::uint8_t buffer[4096];
    const auto received = ::recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (received <= 0) {
      break;
    }
    codec.feed(buffer, static_cast<std::size_t>(received));
    while (auto packet = codec.peekPacket()) {
      packets.push_back(std::move(*packet));
      codec.consumePacket();
    }
  }
  return packets;
}

bool waitForPeerClose(int fd,
                      std::chrono::steady_clock::duration timeout = 1s) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    pollfd entry{};
    entry.fd = fd;
    entry.events = POLLIN;
    const auto ready = ::poll(&entry, 1, 10);
    if (ready < 0 && errno == EINTR) {
      continue;
    }
    if (ready <= 0) {
      continue;
    }

    std::uint8_t byte{};
    const auto received = ::recv(fd, &byte, sizeof(byte), MSG_DONTWAIT);
    if (received == 0) {
      return true;
    }
    if (received < 0 &&
        (errno == ECONNRESET || errno == ENOTCONN || errno == EPIPE)) {
      return true;
    }
  }
  return false;
}

class ConnectionFlood {
 public:
  explicit ConnectionFlood(std::uint16_t port, std::size_t thread_count = 24)
      : port_(port) {
    threads_.reserve(thread_count);
    for (std::size_t index = 0; index < thread_count; ++index) {
      threads_.emplace_back([this] {
        while (running_.load(std::memory_order_acquire)) {
          auto client = connectClient(port_, 50ms);
          if (!client.valid()) {
            std::this_thread::yield();
          }
        }
      });
    }
  }

  ~ConnectionFlood() { stop(); }

  ConnectionFlood(const ConnectionFlood&) = delete;
  ConnectionFlood& operator=(const ConnectionFlood&) = delete;

  void stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
      return;
    }
    for (auto& thread : threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }

 private:
  std::uint16_t port_;
  std::atomic<bool> running_{true};
  std::vector<std::thread> threads_;
};

class BlockingPongHandler final : public rss::service::SessionEventHandler {
 public:
  std::vector<OutboundMessage> handle(const SessionEvent& event) override {
    std::function<void(std::string_view)> packet_observer;
    std::string payload;
    std::size_t response_byte_count = 0;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      ++entered_count_;
      changed_.notify_all();
      released_.wait(lock, [this] { return release_requested_; });

      if (event.kind == SessionEventKind::Disconnected) {
        ++disconnected_count_;
        handled_events_.push_back("Disconnected");
        changed_.notify_all();
        return {};
      }

      payload = rss::protocol::payloadToString(event.packet);
      handled_payloads_.push_back(payload);
      handled_events_.push_back("Packet:" + payload);
      packet_observer = packet_observer_;
      response_byte_count = response_byte_count_;
      changed_.notify_all();
    }

    if (packet_observer) {
      packet_observer(payload);
    }
    if (response_byte_count != 0) {
      return {{event.session_id,
               std::vector<std::uint8_t>(response_byte_count, 0x5aU)}};
    }
    return {{event.session_id, PacketCodec::encode(PacketType::Pong, payload)}};
  }

  void release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      release_requested_ = true;
    }
    released_.notify_all();
  }

  void setPacketObserver(
      std::function<void(std::string_view)> packet_observer) {
    std::lock_guard<std::mutex> lock(mutex_);
    packet_observer_ = std::move(packet_observer);
  }

  void setResponseByteCount(std::size_t response_byte_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    response_byte_count_ = response_byte_count;
  }

  bool waitForEnteredCount(std::size_t count,
                           std::chrono::steady_clock::duration timeout = 1s) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout,
                             [this, count] { return entered_count_ >= count; });
  }

  bool waitForDisconnectedCount(
      std::size_t count, std::chrono::steady_clock::duration timeout = 1s) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(
        lock, timeout, [this, count] { return disconnected_count_ >= count; });
  }

  bool waitForHandledEventCount(
      std::size_t count, std::chrono::steady_clock::duration timeout = 1s) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout, [this, count] {
      return handled_events_.size() >= count;
    });
  }

  [[nodiscard]] std::size_t disconnectedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return disconnected_count_;
  }

  [[nodiscard]] std::vector<std::string> handledPayloads() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return handled_payloads_;
  }

  [[nodiscard]] std::vector<std::string> handledEvents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return handled_events_;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable released_;
  std::condition_variable changed_;
  bool release_requested_{false};
  std::size_t entered_count_{0};
  std::size_t disconnected_count_{0};
  std::vector<std::string> handled_payloads_;
  std::vector<std::string> handled_events_;
  std::function<void(std::string_view)> packet_observer_;
  std::size_t response_byte_count_{0};
};

class SlowClientIsolationHandler final
    : public rss::service::SessionEventHandler {
 public:
  static constexpr std::size_t kLargeResponseBytes = 8U * 1024U * 1024U;

  std::vector<OutboundMessage> handle(const SessionEvent& event) override {
    if (event.kind == SessionEventKind::Disconnected) {
      return {};
    }

    const auto payload = rss::protocol::payloadToString(event.packet);
    if (payload == "register-fast") {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        fast_session_id_ = event.session_id;
      }
      changed_.notify_all();
      return {};
    }

    if (payload != "overflow-slow") {
      return {};
    }

    std::uint64_t fast_session_id = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      fast_session_id = fast_session_id_.value_or(0);
    }

    std::vector<OutboundMessage> messages;
    messages.reserve(3);
    messages.push_back(
        {event.session_id, std::vector<std::uint8_t>(kLargeResponseBytes, 1U)});
    messages.push_back(
        {event.session_id, std::vector<std::uint8_t>(kLargeResponseBytes, 2U)});
    messages.push_back({fast_session_id, PacketCodec::encode(PacketType::Pong,
                                                             "fast-survives")});
    return messages;
  }

  bool waitForFastSession(
      std::chrono::steady_clock::duration timeout = 1s) const {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout,
                             [this] { return fast_session_id_.has_value(); });
  }

 private:
  mutable std::mutex mutex_;
  mutable std::condition_variable changed_;
  std::optional<std::uint64_t> fast_session_id_;
};

class TcpServerBackpressureTest : public testing::Test {
 protected:
  ~TcpServerBackpressureTest() override { stopAndJoin(false); }

  void TearDown() override { stopAndJoin(true); }

  void stopAndJoin(bool report_server_error) {
    handler_.release();
    if (server_ != nullptr) {
      server_->stop();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    if (report_server_error && !server_error_reported_) {
      const auto message = serverErrorMessage();
      if (!message.empty()) {
        ADD_FAILURE() << "server thread failed after readiness: " << message;
      }
      server_error_reported_ = true;
    }
  }

  static rss::net::ServerConfig loopbackConfig() {
    rss::net::ServerConfig config;
    config.host = "127.0.0.1";
    config.port = 0;
    config.worker_count = 1;
    config.max_events = 32;
    return config;
  }

  bool startServer(rss::net::ServerConfig config,
                   rss::service::SessionEventHandler* handler = nullptr) {
    server_ = std::make_unique<rss::net::TcpServer>(
        std::move(config), handler == nullptr ? &handler_ : handler);
    server_thread_ = std::thread([this] {
      try {
        server_->run();
      } catch (...) {
        std::lock_guard<std::mutex> lock(server_error_mutex_);
        server_error_ = std::current_exception();
      }
      {
        std::lock_guard<std::mutex> lock(server_finished_mutex_);
        server_finished_ = true;
      }
      server_finished_changed_.notify_all();
      server_finished_promise_.set_value();
    });

    return waitUntil(
               [this] { return server_->boundPort() != 0 || serverFailed(); },
               1s) &&
           server_->boundPort() != 0;
  }

  [[nodiscard]] bool serverFailed() const {
    std::lock_guard<std::mutex> lock(server_error_mutex_);
    return server_error_ != nullptr;
  }

  [[nodiscard]] std::string serverErrorMessage() const {
    std::exception_ptr error;
    {
      std::lock_guard<std::mutex> lock(server_error_mutex_);
      error = server_error_;
    }
    if (error == nullptr) {
      return {};
    }

    try {
      std::rethrow_exception(error);
    } catch (const std::exception& exception) {
      return exception.what();
    } catch (...) {
      return "unknown exception";
    }
  }

  bool waitForServerFinished(std::chrono::steady_clock::duration timeout = 1s) {
    std::unique_lock<std::mutex> lock(server_finished_mutex_);
    return server_finished_changed_.wait_for(
        lock, timeout, [this] { return server_finished_; });
  }

  std::future_status waitForServerFinishedFuture(
      std::chrono::steady_clock::duration timeout) const {
    return server_finished_future_.wait_for(timeout);
  }

  [[nodiscard]] std::uint16_t boundPort() const { return server_->boundPort(); }

  BlockingPongHandler handler_;
  std::unique_ptr<rss::net::TcpServer> server_;
  std::thread server_thread_;
  mutable std::mutex server_error_mutex_;
  std::exception_ptr server_error_;
  std::mutex server_finished_mutex_;
  std::condition_variable server_finished_changed_;
  bool server_finished_{false};
  std::promise<void> server_finished_promise_;
  std::shared_future<void> server_finished_future_{
      server_finished_promise_.get_future().share()};
  bool server_error_reported_{false};
  SlowClientIsolationHandler slow_client_handler_;
};

TEST_F(TcpServerBackpressureTest,
       RejectsOnlyNewConnectionAtMaximumSessionCount) {
  auto config = loopbackConfig();
  config.max_sessions = 1;
  handler_.release();
  ASSERT_TRUE(startServer(config));

  auto first = connectClient(boundPort());
  ASSERT_TRUE(first.valid());
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 1; }));

  auto second = connectClient(boundPort());
  ASSERT_TRUE(second.valid());
  ASSERT_TRUE(waitUntil([this] {
    const auto snapshot = server_->overloadSnapshot();
    return snapshot.current_sessions == 1 && snapshot.rejected_connections == 1;
  }));
  EXPECT_TRUE(waitForPeerClose(second.get()));

  const std::vector<std::string> payloads{"still-open"};
  ASSERT_TRUE(sendSingleWrite(first.get(), encodePingBatch(payloads)));
  const auto packets = receivePackets(first.get(), 1);
  ASSERT_EQ(packets.size(), 1U);
  EXPECT_EQ(packets[0].type, PacketType::Pong);
  EXPECT_EQ(rss::protocol::payloadToString(packets[0]), "still-open");
}

TEST_F(TcpServerBackpressureTest,
       PreservesPacketOrderAcrossReadPauseAndResume) {
  auto config = loopbackConfig();
  config.inbound_queue_capacity = 4;
  config.inbound_high_watermark = 3;
  config.inbound_low_watermark = 1;
  ASSERT_TRUE(startServer(config));

  auto client = connectClient(boundPort());
  ASSERT_TRUE(client.valid());
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 1; }));

  ASSERT_TRUE(sendSingleWrite(client.get(), encodePingBatch({"gate"})));
  ASSERT_TRUE(handler_.waitForEnteredCount(1));

  const std::vector<std::string> batch{"one",  "two",  "three",
                                       "four", "five", "six"};
  ASSERT_TRUE(sendSingleWrite(client.get(), encodePingBatch(batch)));
  ASSERT_TRUE(waitUntil([this] {
    const auto snapshot = server_->overloadSnapshot();
    return snapshot.read_pauses >= 1 && snapshot.max_inbound_queue_size >= 3;
  }));

  handler_.release();
  const auto packets = receivePackets(client.get(), batch.size() + 1, 3s);
  ASSERT_EQ(packets.size(), batch.size() + 1);

  const std::vector<std::string> expected{"gate", "one",  "two", "three",
                                          "four", "five", "six"};
  for (std::size_t index = 0; index < packets.size(); ++index) {
    EXPECT_EQ(packets[index].type, PacketType::Pong);
    EXPECT_EQ(rss::protocol::payloadToString(packets[index]), expected[index]);
  }

  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().read_resumes >= 1; }));
  EXPECT_EQ(handler_.handledPayloads(), expected);
}

TEST_F(TcpServerBackpressureTest,
       DeliversDeferredDisconnectedEventExactlyOnceAfterCapacityReturns) {
  auto config = loopbackConfig();
  config.inbound_queue_capacity = 4;
  config.inbound_high_watermark = 4;
  config.inbound_low_watermark = 1;
  config.max_sessions = 2;
  ASSERT_TRUE(startServer(config));

  auto packet_client = connectClient(boundPort());
  auto closing_client = connectClient(boundPort());
  ASSERT_TRUE(packet_client.valid());
  ASSERT_TRUE(closing_client.valid());
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 2; }));

  ASSERT_TRUE(sendSingleWrite(packet_client.get(), encodePingBatch({"gate"})));
  ASSERT_TRUE(handler_.waitForEnteredCount(1));

  const std::vector<std::string> batch{"one", "two", "three", "four"};
  ASSERT_TRUE(sendSingleWrite(packet_client.get(), encodePingBatch(batch)));
  ASSERT_TRUE(waitUntil([this] {
    const auto snapshot = server_->overloadSnapshot();
    return snapshot.read_pauses >= 1 && snapshot.max_inbound_queue_size == 4;
  }));

  closing_client.reset();
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 1; }));
  ASSERT_GE(server_->overloadSnapshot().inbound_queue_full, 1U);

  handler_.release();
  const auto packets = receivePackets(packet_client.get(), batch.size() + 1);
  ASSERT_EQ(packets.size(), batch.size() + 1);
  ASSERT_TRUE(handler_.waitForDisconnectedCount(1));
  ASSERT_TRUE(waitUntil([this] {
    return server_->overloadSnapshot().current_inbound_queue_size == 0;
  }));
  EXPECT_TRUE(remainsTrueFor(
      [this] { return handler_.disconnectedCount() == 1; }, 100ms));
}

TEST_F(TcpServerBackpressureTest,
       DrainsClosedSessionPacketsBeforeDisconnectedAfterReadPause) {
  auto config = loopbackConfig();
  config.inbound_queue_capacity = 4;
  config.inbound_high_watermark = 3;
  config.inbound_low_watermark = 1;
  ASSERT_TRUE(startServer(config));

  auto client = connectClient(boundPort());
  ASSERT_TRUE(client.valid());
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 1; }));

  ASSERT_TRUE(sendSingleWrite(client.get(), encodePingBatch({"gate"})));
  ASSERT_TRUE(handler_.waitForEnteredCount(1));

  const std::vector<std::string> batch{"one",  "two",  "three",
                                       "four", "five", "six"};
  ASSERT_TRUE(sendSingleWrite(client.get(), encodePingBatch(batch)));
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().read_pauses >= 1; }));

  client.reset();
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 0; }));
  handler_.release();

  ASSERT_TRUE(handler_.waitForHandledEventCount(8, 2s));
  EXPECT_EQ(handler_.handledEvents(),
            (std::vector<std::string>{
                "Packet:gate", "Packet:one", "Packet:two", "Packet:three",
                "Packet:four", "Packet:five", "Packet:six", "Disconnected"}));
  EXPECT_EQ(handler_.disconnectedCount(), 1U);
}

TEST_F(TcpServerBackpressureTest,
       ReadsReadyPacketBeforeHandlingSimultaneousPeerShutdown) {
  auto config = loopbackConfig();
  config.inbound_queue_capacity = 4;
  config.inbound_high_watermark = 3;
  config.inbound_low_watermark = 1;
  config.max_sessions = 2;
  ASSERT_TRUE(startServer(config));

  auto control = connectClient(boundPort());
  ASSERT_TRUE(control.valid());
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 1; }));

  ASSERT_TRUE(sendSingleWrite(control.get(), encodePingBatch({"gate"})));
  ASSERT_TRUE(handler_.waitForEnteredCount(1));
  ASSERT_TRUE(
      sendSingleWrite(control.get(), encodePingBatch({"one", "two", "three"})));
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().read_pauses >= 1; }));

  auto closing = connectClient(boundPort());
  ASSERT_TRUE(closing.valid());
  ASSERT_TRUE(sendSingleWrite(closing.get(), encodePingBatch({"final"})));
  ASSERT_TRUE(closing.shutdownWrite());
  handler_.release();

  ASSERT_TRUE(handler_.waitForHandledEventCount(6, 2s));
  const auto events = handler_.handledEvents();
  const auto packet = std::find(events.begin(), events.end(), "Packet:final");
  const auto disconnected =
      std::find(events.begin(), events.end(), "Disconnected");
  ASSERT_NE(packet, events.end());
  ASSERT_NE(disconnected, events.end());
  EXPECT_LT(packet, disconnected);
  EXPECT_EQ(handler_.disconnectedCount(), 1U);
}

TEST_F(TcpServerBackpressureTest,
       IsolatesMalformedDeferredCodecAfterDeliveringValidPackets) {
  auto config = loopbackConfig();
  config.inbound_queue_capacity = 4;
  config.inbound_high_watermark = 3;
  config.inbound_low_watermark = 1;
  ASSERT_TRUE(startServer(config));

  auto client = connectClient(boundPort());
  ASSERT_TRUE(client.valid());
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 1; }));

  ASSERT_TRUE(sendSingleWrite(client.get(), encodePingBatch({"gate"})));
  ASSERT_TRUE(handler_.waitForEnteredCount(1));

  auto bytes = encodePingBatch({"one", "two", "three", "four"});
  const std::vector<std::uint8_t> malformed_header{0, 3, 0, 1};
  bytes.insert(bytes.end(), malformed_header.begin(), malformed_header.end());
  ASSERT_TRUE(sendSingleWrite(client.get(), bytes));
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().read_pauses >= 1; }));

  handler_.release();
  ASSERT_TRUE(handler_.waitForHandledEventCount(6, 2s));
  EXPECT_EQ(handler_.handledEvents(),
            (std::vector<std::string>{"Packet:gate", "Packet:one", "Packet:two",
                                      "Packet:three", "Packet:four",
                                      "Disconnected"}));
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 0; }));

  auto survivor = connectClient(boundPort());
  ASSERT_TRUE(survivor.valid());
  ASSERT_TRUE(sendSingleWrite(survivor.get(), encodePingBatch({"alive"})));
  const auto packets = receivePackets(survivor.get(), 1);
  ASSERT_EQ(packets.size(), 1U);
  EXPECT_EQ(rss::protocol::payloadToString(packets[0]), "alive");
}

TEST_F(TcpServerBackpressureTest,
       LimitsAcceptsPerTurnAndKeepsListenerLevelTriggered) {
  auto config = loopbackConfig();
  config.max_sessions = 1;
  config.max_events = 1;
  config.backlog = 512;
  config.inbound_queue_capacity = 4;
  config.inbound_high_watermark = 3;
  config.inbound_low_watermark = 1;
  ASSERT_TRUE(startServer(config));

  auto established = connectClient(boundPort());
  ASSERT_TRUE(established.valid());
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 1; }));

  ASSERT_TRUE(sendSingleWrite(established.get(), encodePingBatch({"gate"})));
  ASSERT_TRUE(handler_.waitForEnteredCount(1));
  ASSERT_TRUE(sendSingleWrite(established.get(),
                              encodePingBatch({"one", "two", "three"})));
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().read_pauses >= 1; }));

  constexpr std::size_t backlog_clients = 128;
  std::vector<ClientSocket> pending_clients;
  pending_clients.reserve(backlog_clients);
  for (std::size_t index = 0; index < backlog_clients; ++index) {
    auto pending = connectClient(boundPort());
    ASSERT_TRUE(pending.valid());
    pending_clients.push_back(std::move(pending));
  }

  std::atomic<std::uint64_t> rejected_at_probe{
      std::numeric_limits<std::uint64_t>::max()};
  handler_.setPacketObserver([this,
                              &rejected_at_probe](std::string_view value) {
    if (value == "probe") {
      rejected_at_probe.store(server_->overloadSnapshot().rejected_connections,
                              std::memory_order_release);
    }
  });
  ASSERT_TRUE(sendSingleWrite(established.get(), encodePingBatch({"probe"})));
  handler_.release();

  ASSERT_TRUE(waitUntil(
      [&rejected_at_probe] {
        return rejected_at_probe.load(std::memory_order_acquire) !=
               std::numeric_limits<std::uint64_t>::max();
      },
      2s));
  EXPECT_LT(rejected_at_probe.load(std::memory_order_acquire), backlog_clients);
  EXPECT_TRUE(waitUntil(
      [this] {
        return server_->overloadSnapshot().rejected_connections ==
               backlog_clients;
      },
      2s));
}

TEST_F(TcpServerBackpressureTest,
       StopRequestInterruptsContinuouslyReadyAcceptLoop) {
  auto config = loopbackConfig();
  config.max_sessions = 1;
  config.max_events = 1;
  config.backlog = 4096;
  handler_.release();
  ASSERT_TRUE(startServer(config));

  auto established = connectClient(boundPort());
  ASSERT_TRUE(established.valid());
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 1; }));

  ConnectionFlood flood(boundPort());
  ASSERT_TRUE(waitUntil(
      [this] {
        return server_->overloadSnapshot().rejected_connections >= 100;
      },
      2s));

  server_->stop();
  const auto stopped_while_flooding = waitForServerFinished(500ms);
  flood.stop();
  EXPECT_TRUE(stopped_while_flooding);
}

TEST_F(TcpServerBackpressureTest,
       DisconnectsOnlySlowSessionAndContinuesOutboundBatch) {
  auto config = loopbackConfig();
  config.max_pending_write_bytes =
      SlowClientIsolationHandler::kLargeResponseBytes;
  config.outbound_queue_capacity = 4;
  config.graceful_shutdown_timeout = 1s;
  ASSERT_TRUE(startServer(config, &slow_client_handler_));

  auto fast = connectClient(boundPort());
  ASSERT_TRUE(fast.valid());
  ASSERT_TRUE(sendSingleWrite(fast.get(), encodePingBatch({"register-fast"})));
  ASSERT_TRUE(slow_client_handler_.waitForFastSession());

  auto slow = connectClient(boundPort(), 1s, 1024);
  ASSERT_TRUE(slow.valid());
  ASSERT_TRUE(waitUntil(
      [this] { return server_->overloadSnapshot().current_sessions == 2; }));
  ASSERT_TRUE(sendSingleWrite(slow.get(), encodePingBatch({"overflow-slow"})));

  ASSERT_TRUE(waitUntil(
      [this] {
        const auto snapshot = server_->overloadSnapshot();
        return snapshot.current_sessions == 1 &&
               snapshot.slow_client_disconnects == 1;
      },
      3s));

  const auto packets = receivePackets(fast.get(), 1, 3s);
  ASSERT_EQ(packets.size(), 1U);
  EXPECT_EQ(packets[0].type, PacketType::Pong);
  EXPECT_EQ(rss::protocol::payloadToString(packets[0]), "fast-survives");

  const auto snapshot = server_->overloadSnapshot();
  EXPECT_EQ(snapshot.slow_client_disconnects, 1U);
  EXPECT_EQ(snapshot.max_session_pending_write_bytes,
            SlowClientIsolationHandler::kLargeResponseBytes);

  stopAndJoin(true);
}

TEST_F(TcpServerBackpressureTest,
       PreservesAlreadyReceivedResponsesDuringGracefulShutdown) {
  auto config = loopbackConfig();
  config.inbound_queue_capacity = 2;
  config.inbound_high_watermark = 2;
  config.inbound_low_watermark = 1;
  config.graceful_shutdown_timeout = 1s;
  ASSERT_TRUE(startServer(config));

  auto client = connectClient(boundPort());
  ASSERT_TRUE(client.valid());
  const std::vector<std::string> payloads{"gate", "one", "two", "three",
                                          "four"};
  ASSERT_TRUE(sendSingleWrite(client.get(), encodePingBatch(payloads)));
  ASSERT_TRUE(handler_.waitForEnteredCount(1));
  ASSERT_TRUE(waitUntil([this] {
    const auto snapshot = server_->overloadSnapshot();
    return snapshot.current_inbound_queue_size == 2 &&
           snapshot.read_pauses >= 1;
  }));

  server_->stop();
  handler_.release();

  const auto packets = receivePackets(client.get(), payloads.size(), 2s);
  ASSERT_EQ(packets.size(), payloads.size());
  for (std::size_t index = 0; index < packets.size(); ++index) {
    EXPECT_EQ(rss::protocol::payloadToString(packets[index]), payloads[index]);
  }
  EXPECT_EQ(waitForServerFinishedFuture(2s), std::future_status::ready);
}

TEST_F(TcpServerBackpressureTest,
       StopsWithinDeadlineWhenInputAndOutputQueuesAreSaturated) {
  auto config = loopbackConfig();
  config.inbound_queue_capacity = 2;
  config.inbound_high_watermark = 2;
  config.inbound_low_watermark = 1;
  config.outbound_queue_capacity = 1;
  config.max_pending_write_bytes = 32U * 1024U * 1024U;
  config.graceful_shutdown_timeout = 1s;
  handler_.setResponseByteCount(8U * 1024U * 1024U);
  ASSERT_TRUE(startServer(config));

  auto client = connectClient(boundPort(), 1s, 1024);
  ASSERT_TRUE(client.valid());
  ASSERT_TRUE(sendSingleWrite(client.get(), encodePingBatch({"gate"})));
  ASSERT_TRUE(handler_.waitForEnteredCount(1));
  ASSERT_TRUE(sendSingleWrite(client.get(), encodePingBatch({"one", "two"})));
  ASSERT_TRUE(waitUntil([this] {
    const auto snapshot = server_->overloadSnapshot();
    return snapshot.current_inbound_queue_size == 2 &&
           snapshot.current_outbound_queue_size == 0;
  }));

  const auto stop_started = std::chrono::steady_clock::now();
  server_->stop();
  EXPECT_EQ(waitForServerFinishedFuture(100ms), std::future_status::timeout);
  handler_.release();

  EXPECT_EQ(waitForServerFinishedFuture(500ms), std::future_status::timeout);
  EXPECT_EQ(waitForServerFinishedFuture(config.graceful_shutdown_timeout + 1s),
            std::future_status::ready);
  const auto elapsed = std::chrono::steady_clock::now() - stop_started;
  EXPECT_GE(elapsed, config.graceful_shutdown_timeout - 100ms);
  EXPECT_LT(elapsed, config.graceful_shutdown_timeout + 1s);
}

}  // namespace
