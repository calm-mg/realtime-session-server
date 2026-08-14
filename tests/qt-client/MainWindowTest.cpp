#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QtTest>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "FakeSessionTransport.h"
#include "application/ClientController.h"
#include "ui/MainWindow.h"

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

  void appendsSystemChatAndErrorLogs() {
    FakeSessionTransport transport;
    rss::qt_client::ClientController controller(transport);
    rss::qt_client::MainWindow window;
    window.bind(controller);

    controller.connectToServer("127.0.0.1", 7777);
    transport.completeConnection();
    transport.receive(packet(rss::protocol::PacketType::RoomBroadcast,
                             "event=CHAT|message=hello"));
    transport.receive(
        packet(rss::protocol::PacketType::Error, "request failed"));

    const QString logs =
        window.findChild<QPlainTextEdit*>("logView")->toPlainText();
    QVERIFY(logs.contains("[시스템]"));
    QVERIFY(logs.contains("[채팅]"));
    QVERIFY(logs.contains("[오류]"));
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
};

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"
