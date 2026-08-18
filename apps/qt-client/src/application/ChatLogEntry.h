#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

#include "application/ClientState.h"

namespace rss::qt_client {

struct ChatLogEntry {
  LogKind kind{LogKind::System};
  QString author;
  QString text;
  QDateTime received_at;
  bool is_own{};
};

}  // namespace rss::qt_client

Q_DECLARE_METATYPE(rss::qt_client::ChatLogEntry)
