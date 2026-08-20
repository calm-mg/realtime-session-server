#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyleOptionViewItem>
#include <QtTest>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ChatBubbleDelegate.h"
#include "ChatLogModel.h"
#include "FakeSessionTransport.h"
#include "rss/qt_client/application/ClientController.h"
#include "rss/qt_client/ui/MainWindow.h"

namespace {

rss::protocol::Packet packet(rss::protocol::PacketType type,
                             std::string_view payload) {
  return {type, std::vector<std::uint8_t>(payload.begin(), payload.end())};
}

void logIn(rss::qt_client::ClientController& controller,
           FakeSessionTransport& transport) {
  controller.connectToServer("127.0.0.1", 7777);
  transport.completeConnection();
  controller.login("alice");
  transport.receive(packet(rss::protocol::PacketType::LoginRes, "OK"));
}

void enterRoom(rss::qt_client::ClientController& controller,
               FakeSessionTransport& transport) {
  logIn(controller, transport);
  controller.createRoom("study");
  transport.receive(
      packet(rss::protocol::PacketType::CreateRoomRes, "OK|room_id=1"));
}

}  // namespace

class MainWindowTest final : public QObject {
  Q_OBJECT

 private slots:
  void keepsControlPanelUsableAtMinimumWindowSize() {
    rss::qt_client::MainWindow window;
    window.resize(860, 560);
    window.show();
    QTest::qWait(50);

    auto* control_panel = window.findChild<QWidget*>("controlPanel");
    QVERIFY(control_panel->height() >=
            control_panel->minimumSizeHint().height());
    auto* scroll_area = window.findChild<QScrollArea*>("controlScrollArea");
    QVERIFY(scroll_area->verticalScrollBar()->maximum() > 0);
  }

  void appliesBundledThemeAndActionIcons() {
    rss::qt_client::MainWindow window;

    QVERIFY(!window.styleSheet().isEmpty());
    const std::array button_names{
        "connectButton",  "disconnectButton", "loginButton", "createRoomButton",
        "joinRoomButton", "leaveRoomButton",  "sendButton",
    };
    for (const char* name : button_names) {
      const QIcon icon = window.findChild<QPushButton*>(name)->icon();
      QVERIFY2(!icon.pixmap(20, 20).isNull(), name);
    }

    const auto* empty_state = window.findChild<QLabel*>("emptyStateLabel");
    QVERIFY(empty_state != nullptr);
    QVERIFY(!empty_state->text().isEmpty());
  }

  void updatesConnectionStatusBadgeAppearance() {
    FakeSessionTransport transport;
    rss::qt_client::ClientController controller(transport);
    rss::qt_client::MainWindow window;
    window.bind(controller);

    auto* status = window.findChild<QLabel*>("connectionStatusLabel");
    QCOMPARE(status->property("status").toString(), QString("offline"));

    controller.connectToServer("127.0.0.1", 7777);
    QCOMPARE(status->property("status").toString(), QString("connecting"));

    transport.completeConnection();
    QCOMPARE(status->property("status").toString(), QString("online"));
  }

  void enablesOnlyConnectionControlsWhenDisconnected() {
    FakeSessionTransport transport;
    rss::qt_client::ClientController controller(transport);
    rss::qt_client::MainWindow window;
    window.bind(controller);

    QVERIFY(window.findChild<QLineEdit*>("hostEdit")->isEnabled());
    QVERIFY(window.findChild<QSpinBox*>("portSpinBox")->isEnabled());
    QVERIFY(window.findChild<QPushButton*>("connectButton")->isEnabled());
    QVERIFY(!window.findChild<QPushButton*>("loginButton")->isEnabled());
    QVERIFY(!window.findChild<QLineEdit*>("messageEdit")->isEnabled());
  }

  void enablesLoginAfterConnection() {
    FakeSessionTransport transport;
    rss::qt_client::ClientController controller(transport);
    rss::qt_client::MainWindow window;
    window.bind(controller);
    controller.connectToServer("127.0.0.1", 7777);
    transport.completeConnection();

    QVERIFY(window.findChild<QPushButton*>("loginButton")->isEnabled());
    QVERIFY(!window.findChild<QPushButton*>("connectButton")->isEnabled());
    QVERIFY(window.findChild<QPushButton*>("disconnectButton")->isEnabled());
  }

  void enablesRoomActionsAfterLogin() {
    FakeSessionTransport transport;
    rss::qt_client::ClientController controller(transport);
    rss::qt_client::MainWindow window;
    window.bind(controller);
    logIn(controller, transport);

    QVERIFY(window.findChild<QPushButton*>("createRoomButton")->isEnabled());
    QVERIFY(window.findChild<QPushButton*>("joinRoomButton")->isEnabled());
    QVERIFY(!window.findChild<QPushButton*>("sendButton")->isEnabled());
  }

  void sendsChatWhenReturnIsPressed() {
    FakeSessionTransport transport;
    rss::qt_client::ClientController controller(transport);
    rss::qt_client::MainWindow window;
    window.bind(controller);
    enterRoom(controller, transport);

    auto* edit = window.findChild<QLineEdit*>("messageEdit");
    QVERIFY(edit->isEnabled());
    edit->setText("hello");
    QTest::keyClick(edit, Qt::Key_Return);

    QVERIFY(transport.lastType() == rss::protocol::PacketType::ChatReq);
    QCOMPARE(transport.lastPayload(), std::string("hello"));
    QVERIFY(edit->text().isEmpty());
  }

  void invokesCreateJoinAndLeaveActions() {
    FakeSessionTransport transport;
    rss::qt_client::ClientController controller(transport);
    rss::qt_client::MainWindow window;
    window.bind(controller);
    logIn(controller, transport);

    auto* room_edit = window.findChild<QLineEdit*>("roomEdit");
    room_edit->setText("study");
    window.findChild<QPushButton*>("createRoomButton")->click();
    QVERIFY(transport.lastType() == rss::protocol::PacketType::CreateRoomReq);
    transport.receive(
        packet(rss::protocol::PacketType::CreateRoomRes, "OK|room_id=1"));

    window.findChild<QPushButton*>("leaveRoomButton")->click();
    QVERIFY(transport.lastType() == rss::protocol::PacketType::LeaveRoomReq);
    transport.receive(packet(rss::protocol::PacketType::LeaveRoomRes, "OK"));

    room_edit->setText("1");
    window.findChild<QPushButton*>("joinRoomButton")->click();
    QVERIFY(transport.lastType() == rss::protocol::PacketType::JoinRoomReq);
    QCOMPARE(transport.lastPayload(), std::string("1"));
  }

  void appendsSystemChatAndErrorAsStructuredRows() {
    FakeSessionTransport transport;
    rss::qt_client::ClientController controller(transport);
    rss::qt_client::MainWindow window;
    window.bind(controller);

    controller.connectToServer("127.0.0.1", 7777);
    transport.completeConnection();
    transport.receive(
        packet(rss::protocol::PacketType::RoomBroadcast,
               "event=CHAT|room_id=1|user_id=2|session_id=11|name=bob|"
               "message=hello"));
    transport.receive(
        packet(rss::protocol::PacketType::Error, "request failed"));

    auto* log_view = window.findChild<QListView*>("logView");
    QVERIFY(log_view != nullptr);
    QCOMPARE(log_view->model()->rowCount(), 3);
    const QModelIndex chat = log_view->model()->index(1, 0);
    QCOMPARE(chat.data(rss::qt_client::ChatLogModel::KindRole)
                 .value<rss::qt_client::LogKind>(),
             rss::qt_client::LogKind::Chat);
    QCOMPARE(chat.data(rss::qt_client::ChatLogModel::AuthorRole).toString(),
             QString("bob"));
    QCOMPARE(chat.data(rss::qt_client::ChatLogModel::TextRole).toString(),
             QString("hello"));
  }

  void scrollsToNewestLogEntry() {
    FakeSessionTransport transport;
    rss::qt_client::ClientController controller(transport);
    rss::qt_client::MainWindow window;
    window.resize(860, 560);
    window.bind(controller);
    window.show();

    for (int i = 0; i < 30; ++i) {
      transport.receive(
          packet(rss::protocol::PacketType::Error, "repeated test error"));
    }

    auto* log_view = window.findChild<QListView*>("logView");
    QTRY_VERIFY(log_view->verticalScrollBar()->maximum() > 0);
    QTRY_COMPARE(log_view->verticalScrollBar()->value(),
                 log_view->verticalScrollBar()->maximum());
  }

  void reservesEnoughHeightForWrappedNoticeText() {
    rss::qt_client::MainWindow window;
    auto* log_view = window.findChild<QListView*>("logView");
    auto* model =
        dynamic_cast<rss::qt_client::ChatLogModel*>(log_view->model());
    auto* delegate = dynamic_cast<rss::qt_client::ChatBubbleDelegate*>(
        log_view->itemDelegate());
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 320, 1);
    option.font = log_view->font();
    option.widget = log_view->viewport();

    const QFontMetrics metrics(option.font);
    QString text;
    int painted_height = 0;
    int wider_height = 0;
    const QStringList tokens{"i",   "ii",   "M",      "MM",    "MMM",
                             "mix", "word", "layout", "notice"};
    for (const QString& token : tokens) {
      text.clear();
      for (int i = 0; i < 200 && painted_height == wider_height; ++i) {
        text += token + " ";
        painted_height =
            metrics
                .boundingRect(QRect(0, 0, 248, 10000),
                              Qt::TextWordWrap | Qt::AlignCenter, text)
                .height();
        wider_height =
            metrics
                .boundingRect(QRect(0, 0, 256, 10000),
                              Qt::TextWordWrap | Qt::AlignCenter, text)
                .height();
      }
      if (painted_height > wider_height) {
        break;
      }
    }
    QVERIFY(painted_height > wider_height);

    model->append({.kind = rss::qt_client::LogKind::Error,
                   .text = text,
                   .received_at = QDateTime::currentDateTime()});
    const QSize hint = delegate->sizeHint(option, model->index(0, 0));
    QVERIFY(hint.height() >= painted_height + 18 + 16);
  }

  void keepsMessageWhenControllerRejectsIt() {
    FakeSessionTransport transport;
    rss::qt_client::ClientController controller(transport);
    rss::qt_client::MainWindow window;
    window.bind(controller);
    enterRoom(controller, transport);

    auto* edit = window.findChild<QLineEdit*>("messageEdit");
    edit->setText("   ");
    window.findChild<QPushButton*>("sendButton")->click();

    QCOMPARE(edit->text(), QString("   "));
  }

  void keepsMessageWhenTransportRejectsSend() {
    FakeSessionTransport transport;
    rss::qt_client::ClientController controller(transport);
    rss::qt_client::MainWindow window;
    window.bind(controller);
    enterRoom(controller, transport);
    transport.setSendSucceeds(false);

    auto* edit = window.findChild<QLineEdit*>("messageEdit");
    edit->setText("hello");
    window.findChild<QPushButton*>("sendButton")->click();

    QCOMPARE(edit->text(), QString("hello"));
  }
};

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"
