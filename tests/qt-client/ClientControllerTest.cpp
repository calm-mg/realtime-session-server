#include <QtTest>
#include <cstdint>
#include <string>

#include "FakeSessionTransport.h"
#include "application/ClientController.h"

using rss::protocol::Packet;
using rss::protocol::PacketType;
using rss::qt_client::ClientController;
using rss::qt_client::ClientState;
using rss::qt_client::LogKind;

class ClientControllerTest final : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() {
    qRegisterMetaType<ClientState>();
    qRegisterMetaType<LogKind>();
    qRegisterMetaType<Packet>();
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

    transport.receive(
        Packet{PacketType::LoginRes, {'O', 'K', '|', 'u', 's', 'e', 'r'}});
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

    transport.fail("Connection refused");

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
        Packet{PacketType::CreateRoomRes, {'O', 'K', '|', 'r', 'o', 'o', 'm'}});
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

    transport.receive(Packet{PacketType::LeaveRoomRes, {'O', 'K'}});
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
    QCOMPARE(log_spy.at(0).at(0).value<LogKind>(), LogKind::Error);
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

  void rejectsBlankChatMessage() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    enterRoom(controller, transport);
    QSignalSpy validation_spy(&controller, &ClientController::validationFailed);
    const int sent_before = transport.sentCount();

    controller.sendChat("   ");

    QCOMPARE(transport.sentCount(), sent_before);
    QCOMPARE(validation_spy.count(), 1);
  }

  void classifiesChatBroadcastAsChatLog() {
    FakeSessionTransport transport;
    ClientController controller(transport);
    QSignalSpy log_spy(&controller, &ClientController::logEntryAdded);

    transport.receive(
        Packet{PacketType::RoomBroadcast,
               {'e', 'v', 'e', 'n', 't', '=', 'C', 'H', 'A', 'T', '|',
                'm', 'e', 's', 's', 'a', 'g', 'e', '=', 'h', 'i'}});

    QCOMPARE(log_spy.count(), 1);
    QCOMPARE(log_spy.at(0).at(0).value<LogKind>(), LogKind::Chat);
  }

 private:
  static void logIn(ClientController& controller,
                    FakeSessionTransport& transport) {
    controller.connectToServer("127.0.0.1", 7777);
    transport.completeConnection();
    controller.login("alice");
    transport.receive(Packet{PacketType::LoginRes, {'O', 'K'}});
  }

  static void enterRoom(ClientController& controller,
                        FakeSessionTransport& transport) {
    logIn(controller, transport);
    controller.createRoom("general");
    transport.receive(Packet{PacketType::CreateRoomRes, {'O', 'K'}});
  }
};

QTEST_GUILESS_MAIN(ClientControllerTest)
#include "ClientControllerTest.moc"
