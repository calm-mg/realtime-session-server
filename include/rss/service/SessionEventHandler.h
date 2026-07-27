#pragma once

#include "rss/service/Command.h"

namespace rss::service {

class OutboundMessageSink {
 public:
  virtual ~OutboundMessageSink() = default;

  [[nodiscard]] virtual bool emit(OutboundMessage message) = 0;
};

class SessionEventHandler {
 public:
  virtual ~SessionEventHandler() = default;

  virtual void handle(const SessionEvent& event, OutboundMessageSink& sink) = 0;
};

}  // namespace rss::service
