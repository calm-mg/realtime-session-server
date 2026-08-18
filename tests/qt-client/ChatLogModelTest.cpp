#include <QSignalSpy>
#include <QtTest>

#include "application/ChatLogEntry.h"
#include "ui/ChatLogModel.h"

using rss::qt_client::ChatLogEntry;
using rss::qt_client::ChatLogModel;
using rss::qt_client::LogKind;

class ChatLogModelTest final : public QObject {
  Q_OBJECT

 private slots:
  void exposesAppendedEntryThroughPresentationRoles() {
    ChatLogModel model;
    QSignalSpy inserted_spy(&model, &QAbstractItemModel::rowsInserted);
    const QDateTime timestamp =
        QDateTime::fromString("2026-08-18T09:30:00+09:00", Qt::ISODate);

    model.append({
        .kind = LogKind::Chat,
        .author = "alice",
        .text = "hello",
        .received_at = timestamp,
        .is_own = true,
    });

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(inserted_spy.count(), 1);
    const QModelIndex index = model.index(0, 0);
    QCOMPARE(index.data(ChatLogModel::KindRole).value<LogKind>(),
             LogKind::Chat);
    QCOMPARE(index.data(ChatLogModel::AuthorRole).toString(), QString("alice"));
    QCOMPARE(index.data(ChatLogModel::TextRole).toString(), QString("hello"));
    QCOMPARE(index.data(ChatLogModel::TimestampRole).toDateTime(), timestamp);
    QVERIFY(index.data(ChatLogModel::OwnRole).toBool());
    QCOMPARE(index.data(Qt::AccessibleTextRole).toString(),
             QString("나, 09:30: hello"));
    QCOMPARE(index.data(Qt::AccessibleDescriptionRole).toString(),
             QString("내 메시지"));
  }
};

QTEST_GUILESS_MAIN(ChatLogModelTest)
#include "ChatLogModelTest.moc"
