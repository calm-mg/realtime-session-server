#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "rss/net/EpollEventLoop.h"
#include "rss/net/EventFdCompletionNotifier.h"
#include "rss/net/OverloadStats.h"
#include "rss/net/ReadBackpressureController.h"
#include "rss/net/ServerConfig.h"
#include "rss/net/Session.h"
#include "rss/net/WorkerPool.h"
#include "rss/service/MessageRouter.h"
#include "rss/service/RoomService.h"
#include "rss/util/BoundedBlockingQueue.h"

namespace rss::net {

class TcpServer {
 public:
  explicit TcpServer(ServerConfig config,
                     service::SessionEventHandler* handler = nullptr);
  ~TcpServer();

  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  void run();
  void stop();
  [[nodiscard]] std::uint16_t boundPort() const noexcept;
  [[nodiscard]] OverloadSnapshot overloadSnapshot() const;

 private:
  enum class ShutdownPhase {
    Running,
    DrainingInput,
    DrainingOutput,
    Forced,
    Complete,
  };

  void openListener();
  void acceptLoop();
  void readSession(int fd);
  void flushSession(int fd);
  void disconnect(int fd);
  void drainOutbound();
  void updateInterest(Session& session);
  void expireIdleSessions();
  void pauseReads();
  void resumeReads();
  bool enqueueDecodedPackets(Session& session);
  bool enqueueDisconnected(std::uint64_t session_id);
  void deferDisconnected(std::unique_ptr<Session> session);
  [[nodiscard]] bool drainDeferredInput();
  void beginShutdown();
  void advanceShutdown();
  void forceShutdown();
  [[nodiscard]] bool allSessionWritesDrained() const;
  [[nodiscard]] int eventLoopWaitTimeoutMs() const;
  void flushAllSessions();
  void closeNetworkResources() noexcept;

  ServerConfig config_;
  EpollEventLoop event_loop_;
  EventFdCompletionNotifier outbound_wakeup_;
  EventFdCompletionNotifier input_capacity_wakeup_;
  util::BoundedBlockingQueue<service::SessionEvent> inbox_;
  util::BoundedBlockingQueue<service::OutboundMessage> outbox_;
  service::RoomService room_service_;
  service::MessageRouter router_;
  service::SessionEventHandler& handler_;
  WorkerPool workers_;
  ReadBackpressureController read_backpressure_;
  OverloadStats overload_stats_;

  int listen_fd_{-1};
  bool listener_registered_{false};
  bool reads_paused_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> running_{false};
  std::atomic<std::uint16_t> bound_port_{0};
  std::atomic<std::size_t> current_sessions_{0};
  ShutdownPhase shutdown_phase_{ShutdownPhase::Running};
  std::chrono::steady_clock::time_point shutdown_deadline_{};
  std::uint64_t next_session_id_{1};
  std::unordered_map<int, std::unique_ptr<Session>> sessions_by_fd_;
  std::unordered_map<std::uint64_t, int> fd_by_session_;
  std::deque<std::unique_ptr<Session>> deferred_disconnects_;
  std::unordered_set<std::uint64_t> deferred_disconnect_ids_;
};

}  // namespace rss::net
