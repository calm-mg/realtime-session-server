#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <cstdint>
#include <string>
#include <vector>

#include "rss/protocol/PacketCodec.h"
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
  void initTestCase() { qRegisterMetaType<rss::protocol::Packet>(); }

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
        rss::protocol::PacketType::LoginRes, "OK|user_id=1");
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
             std::string("OK|user_id=1"));
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

  void emitsProtocolErrorForInvalidHeader() {
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost),
             qPrintable(server.errorString()));
    rss::qt_client::QtSessionClient client;
    QSignalSpy error_spy(&client,
                         &rss::qt_client::SessionTransport::transportError);
    client.connectToHost("127.0.0.1", server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
    auto* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);

    const QByteArray invalid_header{"\x00\x03\x00\x01", 4};
    QCOMPARE(peer->write(invalid_header),
             static_cast<qint64>(invalid_header.size()));

    QTRY_COMPARE_WITH_TIMEOUT(error_spy.count(), 1, 1000);
    QVERIFY(error_spy.at(0).at(0).toString().contains("invalid packet size"));
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

  void rejectsSendWhileDisconnected() {
    rss::qt_client::QtSessionClient client;

    QVERIFY(!client.sendPacket(rss::protocol::PacketType::LoginReq, "alice"));
  }
};

QTEST_GUILESS_MAIN(QtSessionClientTest)
#include "QtSessionClientTest.moc"
