#pragma once

#include "rss/service/Command.h"
#include "rss/service/MessageRouter.h"
#include "rss/util/BlockingQueue.h"

#include <cstddef>
#include <thread>
#include <vector>

namespace rss::net {

class WorkerPool {
public:
    WorkerPool(util::BlockingQueue<service::SessionEvent>& inbox,
               util::BlockingQueue<service::OutboundMessage>& outbox,
               service::MessageRouter& router);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void start(std::size_t thread_count);
    void stop();

private:
    void run();

    util::BlockingQueue<service::SessionEvent>& inbox_;
    util::BlockingQueue<service::OutboundMessage>& outbox_;
    service::MessageRouter& router_;
    std::vector<std::thread> threads_;
};

} // namespace rss::net
