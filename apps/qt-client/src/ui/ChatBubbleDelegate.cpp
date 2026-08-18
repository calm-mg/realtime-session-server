#include "ui/ChatBubbleDelegate.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <algorithm>

#include "application/ClientState.h"
#include "ui/ChatLogModel.h"

namespace rss::qt_client {

namespace {

constexpr int kItemMargin = 8;
constexpr int kBubbleHorizontalPadding = 14;
constexpr int kBubbleVerticalPadding = 11;
constexpr int kHeaderGap = 7;
constexpr int kMinimumBubbleWidth = 170;
constexpr int kMaximumBubbleWidth = 520;

struct EntryLayout {
  int bubble_width{};
  int body_height{};
  int header_height{};

  [[nodiscard]] int totalHeight() const {
    return kItemMargin * 2 + kBubbleVerticalPadding * 2 + header_height +
           kHeaderGap + body_height;
  }
};

int availableWidth(const QStyleOptionViewItem& option) {
  if (option.rect.width() > 0) {
    return option.rect.width();
  }
  return option.widget == nullptr ? 640 : option.widget->width();
}

QFont headerFont(const QFont& base) {
  QFont font(base);
  font.setBold(true);
  if (font.pointSizeF() > 0) {
    font.setPointSizeF(std::max(9.0, font.pointSizeF() - 1.0));
  }
  return font;
}

EntryLayout chatLayout(const QStyleOptionViewItem& option,
                       const QModelIndex& index) {
  const QString author = index.data(ChatLogModel::AuthorRole).toString();
  const QString text = index.data(ChatLogModel::TextRole).toString();
  const QString time =
      index.data(ChatLogModel::TimestampRole).toDateTime().toString("HH:mm");
  const int maximum_width =
      std::clamp(availableWidth(option) * 7 / 10, kMinimumBubbleWidth,
                 kMaximumBubbleWidth);
  const QFontMetrics body_metrics(option.font);
  const QFontMetrics header_metrics(headerFont(option.font));
  const int natural_width =
      std::max(body_metrics.horizontalAdvance(text),
               header_metrics.horizontalAdvance(author + "  " + time)) +
      kBubbleHorizontalPadding * 2;
  const int bubble_width =
      std::clamp(natural_width, kMinimumBubbleWidth, maximum_width);
  const int text_width = bubble_width - kBubbleHorizontalPadding * 2;
  const QRect body_bounds = body_metrics.boundingRect(
      QRect(0, 0, text_width, 10000), Qt::TextWordWrap | Qt::AlignLeft,
      text.isEmpty() ? QStringLiteral(" ") : text);
  return {
      .bubble_width = bubble_width,
      .body_height = std::max(body_metrics.height(), body_bounds.height()),
      .header_height = header_metrics.height(),
  };
}

QRect noticePillRect(const QRect& item_rect) {
  return item_rect.adjusted(24, kItemMargin, -24, -kItemMargin);
}

QRect noticeTextRect(const QRect& pill_rect) {
  return pill_rect.adjusted(12, 6, -12, -6);
}

int noticeHeight(const QStyleOptionViewItem& option, const QModelIndex& index) {
  const int width = std::max(120, availableWidth(option) - 72);
  const QFontMetrics metrics(option.font);
  const QRect bounds = metrics.boundingRect(
      QRect(0, 0, width, 10000), Qt::TextWordWrap | Qt::AlignCenter,
      index.data(ChatLogModel::TextRole).toString());
  return std::max(42, bounds.height() + 18) + kItemMargin * 2;
}

}  // namespace

ChatBubbleDelegate::ChatBubbleDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

QSize ChatBubbleDelegate::sizeHint(const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const {
  const auto kind = index.data(ChatLogModel::KindRole).value<LogKind>();
  const int height = kind == LogKind::Chat
                         ? chatLayout(option, index).totalHeight()
                         : noticeHeight(option, index);
  return {availableWidth(option), height};
}

void ChatBubbleDelegate::paint(QPainter* painter,
                               const QStyleOptionViewItem& option,
                               const QModelIndex& index) const {
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);

  const auto kind = index.data(ChatLogModel::KindRole).value<LogKind>();
  const QString text = index.data(ChatLogModel::TextRole).toString();
  if (kind != LogKind::Chat) {
    const bool error = kind == LogKind::Error;
    const QRect pill = noticePillRect(option.rect);
    painter->setPen(QPen(error ? QColor("#663748") : QColor("#273752")));
    painter->setBrush(error ? QColor("#351d2a") : QColor("#172236"));
    painter->drawRoundedRect(pill, 10, 10);
    painter->setPen(error ? QColor("#f29aa8") : QColor("#8fa0b7"));
    painter->drawText(noticeTextRect(pill), Qt::TextWordWrap | Qt::AlignCenter,
                      text);
    painter->restore();
    return;
  }

  const bool own = index.data(ChatLogModel::OwnRole).toBool();
  const QString author_value = index.data(ChatLogModel::AuthorRole).toString();
  const QString author =
      own ? QStringLiteral("나")
          : (author_value.isEmpty() ? QStringLiteral("알 수 없음")
                                    : author_value);
  const QString time =
      index.data(ChatLogModel::TimestampRole).toDateTime().toString("HH:mm");
  const EntryLayout layout = chatLayout(option, index);
  const int bubble_x =
      own ? option.rect.right() - kItemMargin - layout.bubble_width
          : option.rect.left() + kItemMargin;
  const QRect bubble(bubble_x, option.rect.top() + kItemMargin,
                     layout.bubble_width,
                     option.rect.height() - kItemMargin * 2);

  painter->setPen(Qt::NoPen);
  painter->setBrush(own ? QColor("#6366f1") : QColor("#1b2a41"));
  painter->drawRoundedRect(bubble, 13, 13);

  const QRect content =
      bubble.adjusted(kBubbleHorizontalPadding, kBubbleVerticalPadding,
                      -kBubbleHorizontalPadding, -kBubbleVerticalPadding);
  painter->setFont(headerFont(option.font));
  painter->setPen(own ? QColor("#e8e9ff") : QColor("#9fb0c7"));
  const QFontMetrics header_metrics(painter->font());
  const int time_width = header_metrics.horizontalAdvance(time);
  const QString elided_author = header_metrics.elidedText(
      author, Qt::ElideRight, std::max(0, content.width() - time_width - 12));
  painter->drawText(content, Qt::AlignTop | Qt::AlignLeft, elided_author);
  painter->drawText(content, Qt::AlignTop | Qt::AlignRight, time);

  painter->setFont(option.font);
  painter->setPen(own ? QColor("#ffffff") : QColor("#e5e7eb"));
  const QRect body(content.left(),
                   content.top() + layout.header_height + kHeaderGap,
                   content.width(), layout.body_height);
  painter->drawText(body, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                    text);
  painter->restore();
}

}  // namespace rss::qt_client
