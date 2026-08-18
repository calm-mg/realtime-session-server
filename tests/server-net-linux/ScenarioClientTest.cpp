#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>

#include "EmbeddedServer.h"
#include "ScenarioClient.h"
#include "rss/net/ServerConfig.h"
#include "rss/protocol/PacketCodec.h"

namespace {

using namespace std::chrono_literals;

std::unique_ptr<rss::tools::EmbeddedServer> startTestServer() {
  rss::net::ServerConfig config;
  config.host = "127.0.0.1";
  config.port = 0;
  config.worker_count = 1;

  auto server = std::make_unique<rss::tools::EmbeddedServer>(config);
  server->start(2s);
  return server;
}

TEST(ScenarioClientTest, LogsInCreatesRoomAndReceivesOwnChat) {
  auto server = startTestServer();
  rss::tools::ScenarioClient client;
  client.connect("127.0.0.1", server->port(), 2s);
  client.login("alice", 2s);
  const auto room_id = client.createRoom("room", 2s);
  ASSERT_NE(room_id, 0U);

  client.sendChat("run=1;sender=0;seq=0;sent_us=1", 2s);
  const auto packet = client.receivePacket(2s);
  EXPECT_EQ(packet.type, rss::protocol::PacketType::RoomBroadcast);
  EXPECT_NE(rss::protocol::payloadToString(packet).find("event=CHAT"),
            std::string::npos);
}

TEST(ScenarioClientTest, ReturnsJoinedAndSplitChatPacketsInOrder) {
  auto server = startTestServer();
  rss::tools::ScenarioClient owner;
  owner.connect("127.0.0.1", server->port(), 2s);
  owner.setReceiveBufferBytes(256);
  owner.login("owner", 2s);
  const auto room_id = owner.createRoom("room", 2s);

  rss::tools::ScenarioClient guest;
  guest.connect("127.0.0.1", server->port(), 2s);
  guest.login("guest", 2s);
  guest.joinRoom(room_id, 2s);
  const std::string message =
      "run=1;sender=1;seq=0;sent_us=1;" + std::string(3000, 'x');
  guest.sendChat(message, 2s);

  const auto joined = owner.receivePacket(2s);
  ASSERT_EQ(joined.type, rss::protocol::PacketType::RoomBroadcast);
  EXPECT_NE(rss::protocol::payloadToString(joined).find("event=JOIN|"),
            std::string::npos);

  const auto chat = owner.receivePacket(2s);
  ASSERT_EQ(chat.type, rss::protocol::PacketType::RoomBroadcast);
  const auto chat_payload = rss::protocol::payloadToString(chat);
  EXPECT_NE(chat_payload.find("event=CHAT|"), std::string::npos);
  EXPECT_NE(chat_payload.find(message), std::string::npos);
}

TEST(ScenarioClientTest, IncludesServerErrorPayloadInException) {
  auto server = startTestServer();
  rss::tools::ScenarioClient client;
  client.connect("127.0.0.1", server->port(), 2s);

  try {
    client.joinRoom(1, 2s);
    FAIL() << "joinRoom should report the server error";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("login required"),
              std::string::npos);
  }
}

}  // namespace
