#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>

#include "ChatLogModel.h"
#include "rss/qt_client/application/ChatLogEntry.h"

using rss::qt_client::ChatLogEntry;
using rss::qt_client::ChatLogModel;
using rss::qt_client::LogKind;

class ChatLogModelTest final : public QObject {
  Q_OBJECT

 private slots:
  void removesOldestEntriesWithValidNotifications() {
    ChatLogModel model;
    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    for (int i = 0; i < 1000; ++i) {
      model.append({.text = QString::number(i)});
    }
    QCOMPARE(model.rowCount({}), 1000);
    QCOMPARE(removed.count(), 0);
    QPersistentModelIndex oldest(model.index(0, 0));
    QPersistentModelIndex retained(model.index(1, 0));
    model.append({.text = "1000"});
    QCOMPARE(model.rowCount({}), 1000);
    QCOMPARE(removed.count(), 1);
    QVERIFY(!removed.at(0).at(0).value<QModelIndex>().isValid());
    QCOMPARE(removed.at(0).at(1).toInt(), 0);
    QCOMPARE(removed.at(0).at(2).toInt(), 0);
    QVERIFY(!oldest.isValid());
    QCOMPARE(retained.row(), 0);
    QCOMPARE(retained.data(ChatLogModel::TextRole).toString(), QString("1"));
    for (int i = 1001; i < 1100; ++i) {
      model.append({.text = QString::number(i)});
    }
    QCOMPARE(model.rowCount({}), 1000);
    QCOMPARE(model.index(0, 0).data(ChatLogModel::TextRole).toString(),
             QString("100"));
    QCOMPARE(model.index(999, 0).data(ChatLogModel::TextRole).toString(),
             QString("1099"));
  }

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

    QCOMPARE(model.rowCount({}), 1);
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
