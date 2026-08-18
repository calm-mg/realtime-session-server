#include "ChatLogModel.h"

#include <utility>

namespace rss::qt_client {

ChatLogModel::ChatLogModel(QObject* parent) : QAbstractListModel(parent) {}

int ChatLogModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant ChatLogModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size()) {
    return {};
  }

  const auto& entry = entries_.at(index.row());
  switch (role) {
    case Qt::DisplayRole:
    case TextRole:
      return entry.text;
    case KindRole:
      return QVariant::fromValue(entry.kind);
    case AuthorRole:
      return entry.author;
    case TimestampRole:
      return entry.received_at;
    case OwnRole:
      return entry.is_own;
    case Qt::AccessibleTextRole: {
      QString speaker;
      switch (entry.kind) {
        case LogKind::System:
          speaker = "시스템";
          break;
        case LogKind::Chat:
          speaker = entry.is_own ? QStringLiteral("나") : entry.author;
          if (speaker.isEmpty()) {
            speaker = "알 수 없음";
          }
          break;
        case LogKind::Error:
          speaker = "오류";
          break;
      }
      return QString("%1, %2: %3")
          .arg(speaker, entry.received_at.toString("HH:mm"), entry.text);
    }
    case Qt::AccessibleDescriptionRole:
      switch (entry.kind) {
        case LogKind::System:
          return QStringLiteral("시스템 알림");
        case LogKind::Chat:
          return entry.is_own ? QStringLiteral("내 메시지")
                              : QStringLiteral("받은 메시지");
        case LogKind::Error:
          return QStringLiteral("오류 알림");
      }
      return {};
    default:
      return {};
  }
}

void ChatLogModel::append(ChatLogEntry entry) {
  const int row = static_cast<int>(entries_.size());
  beginInsertRows({}, row, row);
  entries_.append(std::move(entry));
  endInsertRows();
}

}  // namespace rss::qt_client
