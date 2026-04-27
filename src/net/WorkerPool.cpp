#include "rss/net/WorkerPool.h"

#include <utility>

namespace rss::net {

WorkerPool::WorkerPool(util::BlockingQueue<service::SessionEvent>& inbox,
                       util::BlockingQueue<service::OutboundMessage>& outbox,
                       service::MessageRouter& router)
    : inbox_(inbox)
    , outbox_(outbox)
    , router_(router)
{
}

WorkerPool::~WorkerPool()
{
    stop();
}

void WorkerPool::start(std::size_t thread_count)
{
    if (!threads_.empty()) {
        return;
    }
    if (thread_count == 0) {
        thread_count = 1;
    }
    threads_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        threads_.emplace_back([this] { run(); });
    }
}

void WorkerPool::stop()
{
    inbox_.close();
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();
}

void WorkerPool::run()
{
    service::SessionEvent event;
    while (inbox_.pop(event)) {
        auto messages = router_.handle(event);
        for (auto& message : messages) {
            outbox_.push(std::move(message));
        }
    }
}

} // namespace rss::net
