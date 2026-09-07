#pragma once

#include <QAbstractListModel>
#include <QList>

#include "rss/qt_client/application/ChatLogEntry.h"

namespace rss::qt_client {

class ChatLogModel final : public QAbstractListModel {
 public:
  enum Role {
    KindRole = Qt::UserRole + 1,
    AuthorRole,
    TextRole,
    TimestampRole,
    OwnRole,
  };

  explicit ChatLogModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index,
                              int role) const override;

  void append(ChatLogEntry entry);

 private:
  static constexpr int kMaxEntries = 1000;
  QList<ChatLogEntry> entries_;
};

}  // namespace rss::qt_client
