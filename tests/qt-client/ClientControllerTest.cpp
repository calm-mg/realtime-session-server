#include <QtTest>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "FakeSessionTransport.h"
#include "rss/qt_client/application/ClientController.h"

using rss::protocol::Packet;
using rss::protocol::PacketType;
using rss::qt_client::ChatLogEntry;
using rss::qt_client::ClientController;
using rss::qt_client::ClientState;
using rss::qt_client::LogKind;

namespace {

Packet packet(PacketType type, std::string_view payload) {
  return {type, std::vector<std::uint8_t>(payload.begin(), payload.end())};
}

constexpr std::string_view kUserId = "00000000-0000-0000-0000-000000000001";

std::string loginResponse(std::uint64_t session_id = 10,
                          std::string_view name = "alice") {
  return "OK|user_id=" + std::string(kUserId) +
         "|session_id=" + std::to_string(session_id) +
         "|name=" + std::string(name);
}

std::string roomResponse(std::string_view event, std::uint64_t session_id = 10,
                         std::string_view name = "alice") {
  return "OK|event=" + std::string(event) +
         "|room_id=1|user_id=" + std::string(kUserId) +
         "|session_id=" + std::to_string(session_id) +
         "|name=" + std::string(name);
}

}  // namespace

class ClientControllerTest final : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() {
    qRegisterMetaType<ClientState>();
    qRegisterMetaType<ChatLogEntry>();
    qRegisterMetaType<LogKind>();
    qRegisterMetaType<Packet>();
    qRegisterMetaType<rss::qt_client::TransportErrorKind>();
  }

  void waitsForLoginResponseBeforeChangingState() {
    FakeSessionTransport transport;
    ClientController controller(transport);

    controller.connectToServer("127.0.0.1", 7777);
    transport.completeConnection();
    QCOMPARE(controller.state(), ClientState::Connected);

    controller.login("alice");
    QCOMPARE(controller.state(), ClientState::Connected);
    QCOMPARE(static_cast<std::uint16_t>(transport.lastType()),
             static_cast<std::uint16_t>(PacketType::LoginReq));
    QCOMPARE(transport.lastPayload(), std::string("alice"));

    transport.receive(packet(PacketType::LoginRes, loginResponse()));
    QCOMPARE(controller.state(), ClientState::LoggedIn);
  }

  void resetsStateWhenTransportDisconnects() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    controller.connectToServer("127.0.0.1", 7777);
    transport.completeConnection();

    transport.completeDisconnection();

    QCOMPARE(controller.state(), ClientState::Disconnected);
  }

  void resetsStateWhenTransportReportsError() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    controller.connectToServer("127.0.0.1", 7777);
    QCOMPARE(controller.state(), ClientState::Connecting);

    transport.failFatally("Connection refused");

    QCOMPARE(controller.state(), ClientState::Disconnected);
  }

  void rejectsLoginBeforeConnection() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    QSignalSpy validation_spy(&controller, &ClientController::validationFailed);

    controller.login("alice");

    QCOMPARE(transport.sentCount(), 0);
    QCOMPARE(validation_spy.count(), 1);
    QCOMPARE(controller.state(), ClientState::Disconnected);
  }

  void entersRoomOnlyAfterSuccessfulCreateResponse() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    logIn(controller, transport);

    controller.createRoom("general");
    QCOMPARE(controller.state(), ClientState::LoggedIn);
    QCOMPARE(static_cast<std::uint16_t>(transport.lastType()),
             static_cast<std::uint16_t>(PacketType::CreateRoomReq));
    QCOMPARE(transport.lastPayload(), std::string("general"));

    transport.receive(
        packet(PacketType::CreateRoomRes, roomResponse("CREATE_ROOM")));
    QCOMPARE(controller.state(), ClientState::InRoom);
  }

  void returnsToLoggedInAfterSuccessfulLeaveResponse() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    enterRoom(controller, transport);

    controller.leaveRoom();
    QCOMPARE(controller.state(), ClientState::InRoom);
    QCOMPARE(static_cast<std::uint16_t>(transport.lastType()),
             static_cast<std::uint16_t>(PacketType::LeaveRoomReq));
    QCOMPARE(transport.lastPayload(), std::string());

    transport.receive(
        packet(PacketType::LeaveRoomRes, roomResponse("LEAVE_ROOM")));
    QCOMPARE(controller.state(), ClientState::LoggedIn);
  }

  void keepsStateWhenErrorPacketArrives() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    logIn(controller, transport);
    QSignalSpy log_spy(&controller, &ClientController::logEntryAdded);

    transport.receive(
        Packet{PacketType::Error,
               {'n', 'o', 't', ' ', 'a', 'l', 'l', 'o', 'w', 'e', 'd'}});

    QCOMPARE(controller.state(), ClientState::LoggedIn);
    QCOMPARE(log_spy.count(), 1);
    QCOMPARE(log_spy.at(0).at(0).value<ChatLogEntry>().kind, LogKind::Error);
  }

  void rejectsNonNumericRoomId() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    logIn(controller, transport);
    QSignalSpy validation_spy(&controller, &ClientController::validationFailed);
    const int sent_before = transport.sentCount();

    controller.joinRoom("room-one");

    QCOMPARE(transport.sentCount(), sent_before);
    QCOMPARE(validation_spy.count(), 1);
    QCOMPARE(controller.state(), ClientState::LoggedIn);
  }

  void preservesChatWhitespaceAndAllowsEmptyMessage() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    enterRoom(controller, transport);

    QVERIFY(controller.sendChat("  hello  "));
    QCOMPARE(transport.lastPayload(), std::string("  hello  "));
    QVERIFY(controller.sendChat(""));
    QCOMPARE(transport.lastPayload(), std::string());
  }

  void enforcesChatUtf8ByteLimit() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    enterRoom(controller, transport);
    QSignalSpy validation_spy(&controller, &ClientController::validationFailed);

    QVERIFY(controller.sendChat(QString(1291, 'x')));
    QCOMPARE(transport.lastPayload().size(), std::size_t{1291});
    const int sent_before = transport.sentCount();
    QVERIFY(!controller.sendChat(QString(1292, 'x')));

    QCOMPARE(transport.sentCount(), sent_before);
    QCOMPARE(validation_spy.count(), 1);
  }

  void rejectsUserNameAboveUtf8ByteLimit() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    controller.connectToServer("127.0.0.1", 7777);
    transport.completeConnection();
    QSignalSpy validation_spy(&controller, &ClientController::validationFailed);

    controller.login(QString::fromUtf8("가가가가가가가가가가가"));

    QCOMPARE(transport.sentCount(), 0);
    QCOMPARE(validation_spy.count(), 1);
    QCOMPARE(controller.state(), ClientState::Connected);
  }

  void parsesChatBroadcastAndMarksOwnSender() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    controller.connectToServer("127.0.0.1", 7777);
    transport.completeConnection();
    controller.login("alice");
    transport.receive(packet(PacketType::LoginRes, loginResponse()));
    QSignalSpy log_spy(&controller, &ClientController::logEntryAdded);

    transport.receive(packet(PacketType::RoomBroadcast,
                             "event=CHAT|room_id=1|"
                             "user_id=00000000-0000-0000-0000-000000000001|"
                             "session_id=10|name=alice|"
                             "message=hello%7Cthere"));

    QCOMPARE(log_spy.count(), 1);
    const auto entry = log_spy.at(0).at(0).value<ChatLogEntry>();
    QCOMPARE(entry.kind, LogKind::Chat);
    QCOMPARE(entry.author, QString("alice"));
    QCOMPARE(entry.text, QString("hello|there"));
    QVERIFY(entry.is_own);
    QVERIFY(entry.received_at.isValid());
  }

  void decodesEscapedChatAuthorAndMessage() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    controller.connectToServer("127.0.0.1", 7777);
    transport.completeConnection();
    controller.login("alice");
    transport.receive(packet(PacketType::LoginRes, loginResponse()));
    QSignalSpy log_spy(&controller, &ClientController::logEntryAdded);

    transport.receive(packet(
        PacketType::RoomBroadcast,
        "event=CHAT|room_id=1|user_id=00000000-0000-0000-0000-000000000001|"
        "session_id=10|name=kim%7Cadmin%3Dyes%25|message=hello%7Cthere"));

    QCOMPARE(log_spy.count(), 1);
    const auto entry = log_spy.at(0).at(0).value<ChatLogEntry>();
    QCOMPARE(entry.author, QString::fromUtf8("kim|admin=yes%"));
    QCOMPARE(entry.text, QString::fromUtf8("hello|there"));
    QVERIFY(entry.is_own);
  }

  void malformedLoginResponseDoesNotChangeState() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    controller.connectToServer("127.0.0.1", 7777);
    transport.completeConnection();
    controller.login("alice");
    QSignalSpy log_spy(&controller, &ClientController::logEntryAdded);

    transport.receive(packet(PacketType::LoginRes,
                             "OK|user_id=00000000-0000-0000-0000-000000000001|"
                             "session_id=%GG|name=alice"));

    QCOMPARE(controller.state(), ClientState::Connected);
    QCOMPARE(log_spy.count(), 1);
    const auto entry = log_spy.at(0).at(0).value<ChatLogEntry>();
    QCOMPARE(entry.kind, LogKind::Error);
    QCOMPARE(entry.text,
             QString("Protocol error: invalid structured payload."));
  }

 private:
  static void logIn(ClientController& controller,
                    FakeSessionTransport& transport) {
    controller.connectToServer("127.0.0.1", 7777);
    transport.completeConnection();
    controller.login("alice");
    transport.receive(packet(PacketType::LoginRes, loginResponse()));
  }

  static void enterRoom(ClientController& controller,
                        FakeSessionTransport& transport) {
    logIn(controller, transport);
    controller.createRoom("general");
    transport.receive(
        packet(PacketType::CreateRoomRes, roomResponse("CREATE_ROOM")));
  }
};

QTEST_GUILESS_MAIN(ClientControllerTest)
#include "ClientControllerTest.moc"
