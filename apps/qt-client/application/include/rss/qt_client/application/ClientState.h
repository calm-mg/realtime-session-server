#pragma once

#include <QMetaType>

namespace rss::qt_client {

enum class ClientState {
  Disconnected,
  Connecting,
  Connected,
  LoggedIn,
  InRoom,
};

enum class LogKind {
  System,
  Chat,
  Error,
};

}  // namespace rss::qt_client

Q_DECLARE_METATYPE(rss::qt_client::ClientState)
Q_DECLARE_METATYPE(rss::qt_client::LogKind)
