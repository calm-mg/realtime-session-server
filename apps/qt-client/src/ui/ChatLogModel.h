#pragma once

#include <QAbstractListModel>
#include <QList>

#include "application/ChatLogEntry.h"

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

  [[nodiscard]] int rowCount(
      const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index,
                              int role = Qt::DisplayRole) const override;

  void append(ChatLogEntry entry);

 private:
  QList<ChatLogEntry> entries_;
};

}  // namespace rss::qt_client
