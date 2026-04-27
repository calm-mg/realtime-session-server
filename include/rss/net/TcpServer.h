#pragma once

#include "rss/net/EpollEventLoop.h"
#include "rss/net/ServerConfig.h"
#include "rss/net/Session.h"
#include "rss/net/WorkerPool.h"
#include "rss/service/MessageRouter.h"
#include "rss/service/RoomService.h"
#include "rss/util/BlockingQueue.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace rss::net {

class TcpServer {
public:
    explicit TcpServer(ServerConfig config);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    void run();
    void stop();

private:
    void openListener();
    void acceptLoop();
    void readSession(int fd);
    void flushSession(int fd);
    void disconnect(int fd);
    void drainOutbound();
    void updateInterest(Session& session);
    void expireIdleSessions();

    ServerConfig config_;
    EpollEventLoop event_loop_;
    util::BlockingQueue<service::SessionEvent> inbox_;
    util::BlockingQueue<service::OutboundMessage> outbox_;
    service::RoomService room_service_;
    service::MessageRouter router_;
    WorkerPool workers_;

    int listen_fd_{-1};
    bool running_{false};
    std::uint64_t next_session_id_{1};
    std::unordered_map<int, std::unique_ptr<Session>> sessions_by_fd_;
    std::unordered_map<std::uint64_t, int> fd_by_session_;
};

} // namespace rss::net
