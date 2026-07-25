#pragma once

#include <vector>

#include "rss/service/Command.h"

namespace rss::service {

class SessionEventHandler {
 public:
  virtual ~SessionEventHandler() = default;

  [[nodiscard]] virtual std::vector<OutboundMessage> handle(
      const SessionEvent& event) = 0;
};

}  // namespace rss::service
