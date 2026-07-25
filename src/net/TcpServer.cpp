#include "rss/net/TcpServer.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "rss/net/detail/AcceptBatchLimiter.h"
#include "rss/protocol/PacketCodec.h"

namespace rss::net {
namespace {

constexpr std::size_t kAcceptBatchLimit = 64;

int setNonBlocking(int fd) {
  const auto flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }
  return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }

std::runtime_error systemError(const char* message) {
  return std::runtime_error(std::string(message) + ": " + std::strerror(errno));
}

sockaddr_in makeAddress(const std::string& host, std::uint16_t port) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
    throw std::runtime_error("invalid IPv4 bind address: " + host);
  }
  return address;
}

}  // namespace

TcpServer::TcpServer(ServerConfig config, service::SessionEventHandler* handler)
    : config_(std::move(config)),
      inbox_(config_.inbound_queue_capacity),
      outbox_(config_.outbound_queue_capacity),
      router_(room_service_),
      handler_(handler == nullptr
                   ? static_cast<service::SessionEventHandler&>(router_)
                   : *handler),
      workers_(inbox_, outbox_, handler_, config_.inbound_low_watermark,
               &outbound_wakeup_, &input_capacity_wakeup_),
      read_backpressure_(config_.inbound_high_watermark,
                         config_.inbound_low_watermark) {
  config_.validate();
}

TcpServer::~TcpServer() {
  stop();
  if (!running_.load(std::memory_order_acquire)) {
    closeNetworkResources();
  }
}

void TcpServer::run() {
  if (running_.exchange(true, std::memory_order_acq_rel)) {
    throw std::logic_error("server is already running");
  }

  try {
    openListener();
    workers_.start(config_.worker_count);

    std::cout << "rss_server listening on " << config_.host << ':'
              << boundPort() << " with " << config_.worker_count
              << " workers\n";

    while (shutdown_phase_ != ShutdownPhase::Complete) {
      if (stop_requested_.load(std::memory_order_acquire)) {
        beginShutdown();
      }
      advanceShutdown();
      if (shutdown_phase_ == ShutdownPhase::Complete) {
        break;
      }

      auto events =
          event_loop_.wait(eventLoopWaitTimeoutMs(), config_.max_events);
      for (const auto& event : events) {
        if (stop_requested_.load(std::memory_order_acquire)) {
          beginShutdown();
        }

        const auto fd = event.data.fd;
        if (fd == outbound_wakeup_.fd()) {
          outbound_wakeup_.drain();
          drainOutbound();
          continue;
        }

        if (fd == input_capacity_wakeup_.fd()) {
          input_capacity_wakeup_.drain();
          if (shutdown_phase_ == ShutdownPhase::Running ||
              shutdown_phase_ == ShutdownPhase::DrainingInput) {
            static_cast<void>(drainDeferredInput());
          }
          continue;
        }

        if (fd == listen_fd_) {
          if (shutdown_phase_ == ShutdownPhase::Running &&
              listener_registered_ && !reads_paused_) {
            acceptLoop();
          }
          continue;
        }

        if (shutdown_phase_ == ShutdownPhase::Running &&
            (event.events & EPOLLIN) != 0U) {
          readSession(fd);
        }

        if (sessions_by_fd_.contains(fd) &&
            (event.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U) {
          disconnect(fd);
          continue;
        }

        if (sessions_by_fd_.contains(fd) && (event.events & EPOLLOUT) != 0U) {
          flushSession(fd);
        }
      }

      drainOutbound();
      if (shutdown_phase_ == ShutdownPhase::Running) {
        expireIdleSessions();
      }
      advanceShutdown();
    }
  } catch (...) {
    workers_.beginStop();
    outbox_.close();
    workers_.join();
    closeNetworkResources();
    running_.store(false, std::memory_order_release);
    throw;
  }

  workers_.join();
  drainOutbound();
  flushAllSessions();
  closeNetworkResources();
  running_.store(false, std::memory_order_release);
}

void TcpServer::stop() {
  stop_requested_.store(true, std::memory_order_release);
  outbound_wakeup_.notify();
}

std::uint16_t TcpServer::boundPort() const noexcept {
  return bound_port_.load(std::memory_order_acquire);
}

OverloadSnapshot TcpServer::overloadSnapshot() const {
  return overload_stats_.snapshot(
      inbox_.size(), outbox_.size(),
      current_sessions_.load(std::memory_order_relaxed));
}

void TcpServer::openListener() {
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (listen_fd_ < 0) {
    throw systemError("socket failed");
  }

  const int yes = 1;
  if (::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) <
      0) {
    throw systemError("setsockopt SO_REUSEADDR failed");
  }

  if (setNonBlocking(listen_fd_) < 0) {
    throw systemError("failed to set listener non-blocking");
  }

  auto address = makeAddress(config_.host, config_.port);
  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) < 0) {
    throw systemError("bind failed");
  }

  if (::listen(listen_fd_, config_.backlog) < 0) {
    throw systemError("listen failed");
  }

  sockaddr_in bound_address{};
  socklen_t bound_address_size = sizeof(bound_address);
  if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&bound_address),
                    &bound_address_size) < 0) {
    throw systemError("getsockname failed");
  }

  event_loop_.add(listen_fd_, EPOLLIN);
  listener_registered_ = true;
  event_loop_.add(outbound_wakeup_.fd(), EPOLLIN);
  event_loop_.add(input_capacity_wakeup_.fd(), EPOLLIN);
  bound_port_.store(ntohs(bound_address.sin_port), std::memory_order_release);
}

void TcpServer::acceptLoop() {
  if (!listener_registered_ || reads_paused_) {
    return;
  }

  detail::AcceptBatchLimiter limiter(kAcceptBatchLimit);
  while (limiter.tryAcquire(stop_requested_.load(std::memory_order_acquire))) {
    sockaddr_in peer{};
    socklen_t peer_len = sizeof(peer);
    const auto fd = ::accept4(listen_fd_, reinterpret_cast<sockaddr*>(&peer),
                              &peer_len, SOCK_CLOEXEC);
    if (fd < 0) {
      if (wouldBlock()) {
        return;
      }
      throw systemError("accept4 failed");
    }

    if (stop_requested_.load(std::memory_order_acquire)) {
      ::close(fd);
      return;
    }

    if (sessions_by_fd_.size() >= config_.max_sessions) {
      ::close(fd);
      overload_stats_.recordRejectedConnection();
      continue;
    }

    if (setNonBlocking(fd) < 0) {
      ::close(fd);
      continue;
    }

    const auto session_id = next_session_id_++;
    auto session = std::make_unique<Session>(fd, session_id,
                                             config_.max_pending_write_bytes);
    fd_by_session_[session_id] = fd;
    sessions_by_fd_[fd] = std::move(session);
    event_loop_.add(fd, EPOLLIN | EPOLLRDHUP);
    current_sessions_.store(sessions_by_fd_.size(), std::memory_order_relaxed);
  }
}

void TcpServer::readSession(int fd) {
  if (reads_paused_) {
    return;
  }

  auto it = sessions_by_fd_.find(fd);
  if (it == sessions_by_fd_.end()) {
    return;
  }

  auto& session = *it->second;
  std::uint8_t buffer[4096];

  while (true) {
    if (stop_requested_.load(std::memory_order_acquire)) {
      return;
    }

    const auto n = ::recv(fd, buffer, sizeof(buffer), 0);
    if (n == 0) {
      disconnect(fd);
      return;
    }
    if (n < 0) {
      if (wouldBlock()) {
        break;
      }
      disconnect(fd);
      return;
    }

    try {
      session.touch();
      session.codec().feed(buffer, static_cast<std::size_t>(n));
      if (!enqueueDecodedPackets(session)) {
        return;
      }
    } catch (const protocol::ProtocolError&) {
      disconnect(fd);
      return;
    }
  }
}

bool TcpServer::enqueueDecodedPackets(Session& session) {
  while (auto packet = session.codec().peekPacket()) {
    const auto push_result = inbox_.tryPush(service::SessionEvent{
        service::SessionEventKind::Packet, session.id(), std::move(*packet)});
    if (!push_result.succeeded) {
      overload_stats_.recordInboundQueueFull();
      static_cast<void>(
          read_backpressure_.onInboundSize(config_.inbound_queue_capacity));
      pauseReads();
      return false;
    }

    session.codec().consumePacket();
    overload_stats_.observeInboundQueueSize(push_result.size);
    const auto transition = read_backpressure_.onInboundSize(push_result.size);
    if (transition == ReadTransition::Pause) {
      pauseReads();
    }
    if (push_result.size >= config_.inbound_high_watermark) {
      return false;
    }
  }
  return true;
}

void TcpServer::flushSession(int fd) {
  auto it = sessions_by_fd_.find(fd);
  if (it == sessions_by_fd_.end()) {
    return;
  }

  auto& session = *it->second;
  while (session.hasPendingWrite()) {
    auto& write = session.currentWrite();
    const auto* data = write.bytes.data() + write.offset;
    const auto remaining = write.bytes.size() - write.offset;
    const auto n = ::send(fd, data, remaining, MSG_NOSIGNAL);

    if (n == 0) {
      disconnect(fd);
      return;
    }

    if (n < 0) {
      if (wouldBlock()) {
        updateInterest(session);
        return;
      }
      disconnect(fd);
      return;
    }

    session.consumeWrite(static_cast<std::size_t>(n));
  }

  updateInterest(session);
}

void TcpServer::disconnect(int fd) {
  auto it = sessions_by_fd_.find(fd);
  if (it == sessions_by_fd_.end()) {
    return;
  }

  auto session = std::move(it->second);
  const auto session_id = session->id();
  fd_by_session_.erase(session_id);
  event_loop_.remove(fd);
  ::close(fd);
  sessions_by_fd_.erase(it);
  current_sessions_.store(sessions_by_fd_.size(), std::memory_order_relaxed);

  if (shutdown_phase_ != ShutdownPhase::Running ||
      stop_requested_.load(std::memory_order_acquire)) {
    return;
  }

  bool has_completed_packet = false;
  try {
    has_completed_packet = session->codec().peekPacket().has_value();
  } catch (const protocol::ProtocolError&) {
  }

  if (has_completed_packet || !enqueueDisconnected(session_id)) {
    deferDisconnected(std::move(session));
  }
}

void TcpServer::drainOutbound() {
  while (true) {
    auto result = outbox_.tryPop();
    if (!result.value.has_value()) {
      break;
    }
    auto message = std::move(result.value);
    const auto fd_it = fd_by_session_.find(message->session_id);
    if (fd_it == fd_by_session_.end()) {
      continue;
    }

    auto session_it = sessions_by_fd_.find(fd_it->second);
    if (session_it == sessions_by_fd_.end()) {
      continue;
    }

    auto& session = *session_it->second;
    if (!session.tryEnqueue(std::move(message->bytes))) {
      overload_stats_.recordSlowClientDisconnect();
      disconnect(session.fd());
      continue;
    }

    overload_stats_.observeSessionPendingWriteBytes(
        session.pendingWriteBytes());
    updateInterest(session);
  }
}

void TcpServer::updateInterest(Session& session) {
  auto events = static_cast<std::uint32_t>(EPOLLRDHUP);
  if (shutdown_phase_ == ShutdownPhase::Running && !reads_paused_) {
    events |= EPOLLIN;
  }
  if (session.hasPendingWrite()) {
    events |= EPOLLOUT;
  }
  event_loop_.modify(session.fd(), events);
}

void TcpServer::expireIdleSessions() {
  const auto now = std::chrono::steady_clock::now();
  std::vector<int> expired;
  for (const auto& [fd, session] : sessions_by_fd_) {
    if (now - session->lastSeen() > config_.idle_timeout) {
      expired.push_back(fd);
    }
  }
  for (const auto fd : expired) {
    disconnect(fd);
  }
}

void TcpServer::pauseReads() {
  if (reads_paused_) {
    return;
  }

  reads_paused_ = true;
  if (listener_registered_) {
    event_loop_.remove(listen_fd_);
    listener_registered_ = false;
  }
  for (auto& [_, session] : sessions_by_fd_) {
    updateInterest(*session);
  }
  overload_stats_.recordReadPause();
}

void TcpServer::resumeReads() {
  if (!reads_paused_ || shutdown_phase_ != ShutdownPhase::Running) {
    return;
  }

  reads_paused_ = false;
  if (!listener_registered_) {
    event_loop_.add(listen_fd_, EPOLLIN);
    listener_registered_ = true;
  }
  for (auto& [_, session] : sessions_by_fd_) {
    updateInterest(*session);
  }
  overload_stats_.recordReadResume();
}

bool TcpServer::enqueueDisconnected(std::uint64_t session_id) {
  const auto push_result = inbox_.tryPush(service::SessionEvent{
      service::SessionEventKind::Disconnected, session_id, {}});
  if (!push_result.succeeded) {
    overload_stats_.recordInboundQueueFull();
    static_cast<void>(
        read_backpressure_.onInboundSize(config_.inbound_queue_capacity));
    pauseReads();
    return false;
  }

  overload_stats_.observeInboundQueueSize(push_result.size);
  if (read_backpressure_.onInboundSize(push_result.size) ==
      ReadTransition::Pause) {
    pauseReads();
  }
  return true;
}

void TcpServer::deferDisconnected(std::unique_ptr<Session> session) {
  const auto session_id = session->id();
  if (deferred_disconnect_ids_.contains(session_id)) {
    return;
  }
  if (deferred_disconnects_.size() >= config_.max_sessions) {
    throw std::logic_error("deferred disconnect limit exceeded");
  }

  deferred_disconnects_.push_back(std::move(session));
  deferred_disconnect_ids_.insert(session_id);
}

bool TcpServer::drainDeferredInput() {
  while (!deferred_disconnects_.empty()) {
    auto& session = *deferred_disconnects_.front();
    try {
      if (!enqueueDecodedPackets(session)) {
        return false;
      }
    } catch (const protocol::ProtocolError&) {
    }

    const auto session_id = session.id();
    if (!enqueueDisconnected(session_id)) {
      return false;
    }

    deferred_disconnects_.pop_front();
    deferred_disconnect_ids_.erase(session_id);
    if (inbox_.size() >= config_.inbound_high_watermark) {
      return false;
    }
  }

  std::vector<int> protocol_error_fds;
  for (auto& [fd, session] : sessions_by_fd_) {
    try {
      if (!enqueueDecodedPackets(*session)) {
        return false;
      }
    } catch (const protocol::ProtocolError&) {
      protocol_error_fds.push_back(fd);
    }
  }
  for (const auto fd : protocol_error_fds) {
    disconnect(fd);
    if (inbox_.size() >= config_.inbound_high_watermark) {
      return false;
    }
  }

  const auto inbound_size = inbox_.size();
  if (shutdown_phase_ == ShutdownPhase::Running &&
      read_backpressure_.onCapacityAvailable(inbound_size) ==
          ReadTransition::Resume) {
    resumeReads();
  }
  return true;
}

void TcpServer::beginShutdown() {
  if (shutdown_phase_ != ShutdownPhase::Running) {
    return;
  }

  shutdown_phase_ = ShutdownPhase::DrainingInput;
  shutdown_deadline_ =
      std::chrono::steady_clock::now() + config_.graceful_shutdown_timeout;
  reads_paused_ = true;

  if (listener_registered_) {
    event_loop_.remove(listen_fd_);
    listener_registered_ = false;
  }
  for (auto& [_, session] : sessions_by_fd_) {
    updateInterest(*session);
  }
}

void TcpServer::advanceShutdown() {
  if (shutdown_phase_ == ShutdownPhase::Running ||
      shutdown_phase_ == ShutdownPhase::Complete) {
    return;
  }

  drainOutbound();

  if (shutdown_phase_ == ShutdownPhase::DrainingInput && drainDeferredInput()) {
    workers_.beginStop();
    shutdown_phase_ = ShutdownPhase::DrainingOutput;
  }

  drainOutbound();
  if (shutdown_phase_ == ShutdownPhase::DrainingOutput && workers_.finished()) {
    drainOutbound();
    if (outbox_.size() == 0 && allSessionWritesDrained()) {
      outbox_.close();
      shutdown_phase_ = ShutdownPhase::Complete;
      return;
    }
  }

  if (shutdown_phase_ != ShutdownPhase::Forced &&
      std::chrono::steady_clock::now() >= shutdown_deadline_) {
    forceShutdown();
  }

  if (shutdown_phase_ == ShutdownPhase::Forced) {
    drainOutbound();
    if (workers_.finished()) {
      drainOutbound();
      shutdown_phase_ = ShutdownPhase::Complete;
    }
  }
}

void TcpServer::forceShutdown() {
  if (shutdown_phase_ == ShutdownPhase::Forced ||
      shutdown_phase_ == ShutdownPhase::Complete) {
    return;
  }

  workers_.beginStop();
  outbox_.close();
  shutdown_phase_ = ShutdownPhase::Forced;
}

bool TcpServer::allSessionWritesDrained() const {
  return std::all_of(
      sessions_by_fd_.begin(), sessions_by_fd_.end(),
      [](const auto& entry) { return !entry.second->hasPendingWrite(); });
}

int TcpServer::eventLoopWaitTimeoutMs() const {
  if (shutdown_phase_ == ShutdownPhase::Running) {
    return 1000;
  }
  if (shutdown_phase_ == ShutdownPhase::Forced) {
    return 10;
  }
  if (shutdown_phase_ == ShutdownPhase::Complete) {
    return 0;
  }

  const auto remaining = shutdown_deadline_ - std::chrono::steady_clock::now();
  if (remaining <= std::chrono::steady_clock::duration::zero()) {
    return 0;
  }
  const auto remaining_ms =
      std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
  return static_cast<int>(std::min<std::int64_t>(10, remaining_ms));
}

void TcpServer::flushAllSessions() {
  std::vector<int> session_fds;
  session_fds.reserve(sessions_by_fd_.size());
  for (const auto& [fd, _] : sessions_by_fd_) {
    session_fds.push_back(fd);
  }
  for (const auto fd : session_fds) {
    if (sessions_by_fd_.contains(fd)) {
      flushSession(fd);
    }
  }
}

void TcpServer::closeNetworkResources() noexcept {
  if (listener_registered_ && listen_fd_ >= 0) {
    event_loop_.remove(listen_fd_);
  }
  listener_registered_ = false;

  for (const auto& [fd, _] : sessions_by_fd_) {
    event_loop_.remove(fd);
    ::close(fd);
  }
  sessions_by_fd_.clear();
  fd_by_session_.clear();
  deferred_disconnects_.clear();
  deferred_disconnect_ids_.clear();
  current_sessions_.store(0, std::memory_order_relaxed);

  if (listen_fd_ >= 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  bound_port_.store(0, std::memory_order_release);
}

}  // namespace rss::net
