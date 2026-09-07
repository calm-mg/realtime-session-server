#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rss/protocol/PacketCodec.h"
#include "rss/qt_client/application/ClientController.h"
#include "rss/qt_client/network/QtSessionClient.h"

namespace {

QByteArray toByteArray(const std::vector<std::uint8_t>& bytes) {
  return {reinterpret_cast<const char*>(bytes.data()),
          static_cast<qsizetype>(bytes.size())};
}

}  // namespace

class QtSessionClientTest final : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() {
    qRegisterMetaType<rss::protocol::Packet>();
    qRegisterMetaType<rss::qt_client::ClientState>();
    qRegisterMetaType<rss::qt_client::TransportErrorKind>();
  }

  void emitsConnectedAndDisconnected() {
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost),
             qPrintable(server.errorString()));
    rss::qt_client::QtSessionClient client;
    QSignalSpy connected_spy(&client,
                             &rss::qt_client::SessionTransport::connected);
    QSignalSpy disconnected_spy(
        &client, &rss::qt_client::SessionTransport::disconnected);

    client.connectToHost("127.0.0.1", server.serverPort());

    QTRY_COMPARE_WITH_TIMEOUT(connected_spy.count(), 1, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
    auto* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);

    peer->disconnectFromHost();
    QTRY_COMPARE_WITH_TIMEOUT(disconnected_spy.count(), 1, 1000);
  }

  void receivesPacketSplitAcrossWrites() {
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost),
             qPrintable(server.errorString()));
    rss::qt_client::QtSessionClient client;
    QSignalSpy packet_spy(&client,
                          &rss::qt_client::SessionTransport::packetReceived);
    client.connectToHost("127.0.0.1", server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
    auto* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);

    const auto bytes = rss::protocol::PacketCodec::encode(
        rss::protocol::PacketType::LoginRes,
        "OK|user_id=00000000-0000-0000-0000-000000000001");
    QCOMPARE(peer->write(reinterpret_cast<const char*>(bytes.data()), 2), 2);
    peer->flush();
    QTest::qWait(10);
    QCOMPARE(packet_spy.count(), 0);

    const auto remainder = static_cast<qint64>(bytes.size() - 2);
    QCOMPARE(
        peer->write(reinterpret_cast<const char*>(bytes.data() + 2), remainder),
        remainder);
    QTRY_COMPARE_WITH_TIMEOUT(packet_spy.count(), 1, 1000);

    const auto packet = packet_spy.at(0).at(0).value<rss::protocol::Packet>();
    QCOMPARE(rss::protocol::payloadToString(packet),
             std::string("OK|user_id=00000000-0000-0000-0000-000000000001"));
  }

  void receivesMultiplePacketsFromOneWrite() {
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost),
             qPrintable(server.errorString()));
    rss::qt_client::QtSessionClient client;
    QSignalSpy packet_spy(&client,
                          &rss::qt_client::SessionTransport::packetReceived);
    client.connectToHost("127.0.0.1", server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
    auto* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);

    const auto login = rss::protocol::PacketCodec::encode(
        rss::protocol::PacketType::LoginRes, "OK");
    const auto broadcast = rss::protocol::PacketCodec::encode(
        rss::protocol::PacketType::RoomBroadcast, "event=CHAT|message=hi");
    QByteArray bytes = toByteArray(login);
    bytes.append(toByteArray(broadcast));
    QCOMPARE(peer->write(bytes), static_cast<qint64>(bytes.size()));

    QTRY_COMPARE_WITH_TIMEOUT(packet_spy.count(), 2, 1000);
    const auto second = packet_spy.at(1).at(0).value<rss::protocol::Packet>();
    QCOMPARE(rss::protocol::payloadToString(second),
             std::string("event=CHAT|message=hi"));
  }

  void negotiatesOverSocketAndRejectsUnsupportedVersion() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    rss::qt_client::QtSessionClient client;
    rss::qt_client::ClientController controller(client);
    controller.connectToServer("127.0.0.1", server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
    auto* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    const auto request = toByteArray(rss::protocol::PacketCodec::encode(
        rss::protocol::PacketType::VersionReq, "min_version=1|max_version=1"));
    QTRY_COMPARE_WITH_TIMEOUT(peer->bytesAvailable(), request.size(), 1000);
    QCOMPARE(peer->readAll(), request);
    QCOMPARE(controller.state(), rss::qt_client::ClientState::Connecting);
    const auto response = toByteArray(rss::protocol::PacketCodec::encode(
        rss::protocol::PacketType::VersionRes, "OK|version=2"));
    peer->write(response.first(2));
    QTest::qWait(20);
    QCOMPARE(controller.state(), rss::qt_client::ClientState::Connecting);
    peer->write(response.sliced(2));
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(),
                              rss::qt_client::ClientState::Disconnected, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(peer->state(), QAbstractSocket::UnconnectedState,
                              1000);
  }

  void reconnectsOnceAfterFatalProtocolError() {
    QTcpServer first_server;
    QVERIFY2(first_server.listen(QHostAddress::LocalHost),
             qPrintable(first_server.errorString()));
    QTcpServer second_server;
    QVERIFY2(second_server.listen(QHostAddress::LocalHost),
             qPrintable(second_server.errorString()));
    rss::qt_client::QtSessionClient client;
    rss::qt_client::ClientController controller(client);
    QSignalSpy error_spy(&client,
                         &rss::qt_client::SessionTransport::transportError);
    QSignalSpy state_spy(&controller,
                         &rss::qt_client::ClientController::stateChanged);
    bool reconnect_requested = false;
    connect(&controller, &rss::qt_client::ClientController::stateChanged, this,
            [&](rss::qt_client::ClientState state) {
              if (state == rss::qt_client::ClientState::Disconnected &&
                  !reconnect_requested) {
                reconnect_requested = true;
                controller.connectToServer("127.0.0.1",
                                           second_server.serverPort());
              }
            });

    controller.connectToServer("127.0.0.1", first_server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(first_server.hasPendingConnections(), 1000);
    auto* peer = first_server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    const auto version_response = rss::protocol::PacketCodec::encode(
        rss::protocol::PacketType::VersionRes, "OK|version=1");
    peer->write(toByteArray(version_response));
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(),
                              rss::qt_client::ClientState::Connected, 1000);
    state_spy.clear();

    const QByteArray invalid_header{"\x00\x03\x00\x01", 4};
    QCOMPARE(peer->write(invalid_header),
             static_cast<qint64>(invalid_header.size()));

    QTRY_COMPARE_WITH_TIMEOUT(error_spy.count(), 1, 1000);
    QCOMPARE(error_spy.at(0).at(0).value<rss::qt_client::TransportErrorKind>(),
             rss::qt_client::TransportErrorKind::Fatal);
    QVERIFY(error_spy.at(0).at(1).toString().contains("invalid packet size"));
    QTRY_COMPARE_WITH_TIMEOUT(peer->state(), QAbstractSocket::UnconnectedState,
                              1000);
    QTRY_VERIFY_WITH_TIMEOUT(second_server.hasPendingConnections(), 1000);
    auto* second_peer = second_server.nextPendingConnection();
    QVERIFY(second_peer != nullptr);
    second_peer->write(toByteArray(version_response));
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(),
                              rss::qt_client::ClientState::Connected, 1000);

    int disconnected_transitions = 0;
    for (const auto& arguments : state_spy) {
      if (arguments.at(0).value<rss::qt_client::ClientState>() ==
          rss::qt_client::ClientState::Disconnected) {
        ++disconnected_transitions;
      }
    }
    QCOMPARE(disconnected_transitions, 1);
  }

  void reconnectsAfterFatalSocketError() {
    rss::qt_client::QtSessionClient client;
    rss::qt_client::ClientController controller(client);
    QSignalSpy error_spy(&client,
                         &rss::qt_client::SessionTransport::transportError);

    client.connectToHost("127.0.0.1", 0);

    QTRY_COMPARE_WITH_TIMEOUT(error_spy.count(), 1, 1000);
    QCOMPARE(error_spy.at(0).at(0).value<rss::qt_client::TransportErrorKind>(),
             rss::qt_client::TransportErrorKind::Fatal);
    QCOMPARE(controller.state(), rss::qt_client::ClientState::Disconnected);

    QTcpServer available_server;
    QVERIFY2(available_server.listen(QHostAddress::LocalHost),
             qPrintable(available_server.errorString()));
    controller.connectToServer("127.0.0.1", available_server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(available_server.hasPendingConnections(), 1000);
    auto* peer = available_server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    peer->write(toByteArray(rss::protocol::PacketCodec::encode(
        rss::protocol::PacketType::VersionRes, "OK|version=1")));
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(),
                              rss::qt_client::ClientState::Connected, 1000);
    QCOMPARE(error_spy.count(), 1);
  }

  void sendsEncodedPacketsInOrder() {
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost),
             qPrintable(server.errorString()));
    rss::qt_client::QtSessionClient client;
    client.connectToHost("127.0.0.1", server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
    auto* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);

    QVERIFY(client.sendPacket(rss::protocol::PacketType::LoginReq, "alice"));
    QVERIFY(client.sendPacket(rss::protocol::PacketType::ChatReq, "hello"));

    QByteArray expected = toByteArray(rss::protocol::PacketCodec::encode(
        rss::protocol::PacketType::LoginReq, "alice"));
    expected.append(toByteArray(rss::protocol::PacketCodec::encode(
        rss::protocol::PacketType::ChatReq, "hello")));
    QTRY_VERIFY_WITH_TIMEOUT(peer->bytesAvailable() >= expected.size(), 1000);
    QCOMPARE(peer->read(expected.size()), expected);
  }

  void keepsConnectionUsableAfterOversizedOutgoingPayload() {
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost),
             qPrintable(server.errorString()));
    rss::qt_client::QtSessionClient client;
    rss::qt_client::ClientController controller(client);
    QSignalSpy error_spy(&client,
                         &rss::qt_client::SessionTransport::transportError);

    controller.connectToServer("127.0.0.1", server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
    auto* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    const auto request = toByteArray(rss::protocol::PacketCodec::encode(
        rss::protocol::PacketType::VersionReq, "min_version=1|max_version=1"));
    QTRY_COMPARE_WITH_TIMEOUT(peer->bytesAvailable(), request.size(), 1000);
    QCOMPARE(peer->readAll(), request);
    peer->write(toByteArray(rss::protocol::PacketCodec::encode(
        rss::protocol::PacketType::VersionRes, "OK|version=1")));
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(),
                              rss::qt_client::ClientState::Connected, 1000);

    const std::string oversized_payload(
        rss::protocol::kMaxPacketSize - rss::protocol::kPacketHeaderSize + 1,
        'x');
    QVERIFY(!client.sendPacket(rss::protocol::PacketType::LoginReq,
                               oversized_payload));

    QCOMPARE(error_spy.count(), 1);
    QCOMPARE(error_spy.at(0).at(0).value<rss::qt_client::TransportErrorKind>(),
             rss::qt_client::TransportErrorKind::Recoverable);
    QCOMPARE(controller.state(), rss::qt_client::ClientState::Connected);
    QVERIFY(
        client.sendPacket(rss::protocol::PacketType::Ping, std::string_view{}));

    const auto expected = rss::protocol::PacketCodec::encode(
        rss::protocol::PacketType::Ping, std::string_view{});
    QTRY_VERIFY_WITH_TIMEOUT(
        peer->bytesAvailable() >= static_cast<qint64>(expected.size()), 1000);
    QCOMPARE(peer->read(static_cast<qint64>(expected.size())),
             toByteArray(expected));
  }

  void rejectsQueueOverflowAndResumesAfterDrain() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    rss::qt_client::QtSessionClient client;
    QSignalSpy connected(&client, &rss::qt_client::SessionTransport::connected);
    QSignalSpy errors(&client,
                      &rss::qt_client::SessionTransport::transportError);
    QSignalSpy disconnected(&client,
                            &rss::qt_client::SessionTransport::disconnected);
    client.connectToHost("127.0.0.1", server.serverPort());
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
    auto* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    const std::string payload(4092, 'x');
    // 이벤트 루프를 진행하지 않아 Qt 송신 버퍼를 정확히 1 MiB 채운다.
    for (int i = 0; i < 256; ++i) {
      QVERIFY(client.sendPacket(rss::protocol::PacketType::ChatReq, payload));
    }
    QVERIFY(!client.sendPacket(rss::protocol::PacketType::Ping,
                               std::string_view{}));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(errors.at(0).at(0).value<rss::qt_client::TransportErrorKind>(),
             rss::qt_client::TransportErrorKind::Recoverable);
    QVERIFY(!errors.at(0).at(1).toString().isEmpty());
    QCOMPARE(disconnected.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(peer->bytesAvailable(), 1048576, 3000);
    rss::protocol::PacketCodec codec;
    const auto bytes = peer->readAll();
    codec.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
               static_cast<std::size_t>(bytes.size()));
    const auto packets = codec.drainPackets();
    QCOMPARE(packets.size(), std::size_t{256});
    for (const auto& packet : packets) {
      QCOMPARE(rss::protocol::payloadToString(packet), payload);
    }
    QVERIFY(
        client.sendPacket(rss::protocol::PacketType::Ping, std::string_view{}));
    QTRY_COMPARE_WITH_TIMEOUT(peer->bytesAvailable(), 4, 1000);
    QCOMPARE(peer->readAll(),
             toByteArray(rss::protocol::PacketCodec::encode(
                 rss::protocol::PacketType::Ping, std::string_view{})));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(disconnected.count(), 0);
  }

  void rejectsSendWhileDisconnected() {
    rss::qt_client::QtSessionClient client;

    QVERIFY(!client.sendPacket(rss::protocol::PacketType::LoginReq, "alice"));
  }
};

QTEST_GUILESS_MAIN(QtSessionClientTest)
#include "QtSessionClientTest.moc"
