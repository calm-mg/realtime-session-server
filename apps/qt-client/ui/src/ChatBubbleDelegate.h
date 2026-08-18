#pragma once

#include <QStyledItemDelegate>

namespace rss::qt_client {

class ChatBubbleDelegate final : public QStyledItemDelegate {
 public:
  explicit ChatBubbleDelegate(QObject* parent = nullptr);

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                               const QModelIndex& index) const override;
};

}  // namespace rss::qt_client
