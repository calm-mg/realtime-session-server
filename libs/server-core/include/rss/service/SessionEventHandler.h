#pragma once

#include <memory>
#include <vector>

#include "rss/service/Command.h"

namespace rss::service {

class OutboundMessageSink {
 public:
  virtual ~OutboundMessageSink() = default;

  [[nodiscard]] virtual bool emit(OutboundMessage message) = 0;
};

class DeferredSessionCompletion {
 public:
  virtual ~DeferredSessionCompletion() = default;

  [[nodiscard]] virtual bool succeed(std::vector<OutboundMessage> messages) = 0;
  [[nodiscard]] virtual bool fail() = 0;
};

class SessionEventContext : public OutboundMessageSink {
 public:
  [[nodiscard]] virtual std::shared_ptr<DeferredSessionCompletion> defer() = 0;
};

class SessionEventHandler {
 public:
  virtual ~SessionEventHandler() = default;

  virtual void handle(const SessionEvent& event,
                      SessionEventContext& context) = 0;
};

}  // namespace rss::service
