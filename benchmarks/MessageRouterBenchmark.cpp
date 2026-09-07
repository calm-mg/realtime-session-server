#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "rss/persistence/InMemoryUserRepository.h"
#include "rss/protocol/PacketCodec.h"
#include "rss/protocol/ProtocolVersion.h"
#include "rss/protocol/StructuredPayload.h"
#include "rss/service/MessageRouter.h"

namespace {

class CountingSink final : public rss::service::SessionEventContext {
 public:
  bool emit(rss::service::OutboundMessage message) override {
    benchmark::DoNotOptimize(message);
    ++count_;
    return true;
  }

  [[nodiscard]] std::size_t count() const { return count_; }

  std::shared_ptr<rss::service::DeferredSessionCompletion> defer() override {
    throw std::logic_error("chat benchmark must not defer");
  }

 private:
  std::size_t count_{};
};

class RecordingSink final : public rss::service::SessionEventContext {
 public:
  bool emit(rss::service::OutboundMessage message) override {
    messages.push_back(std::move(message));
    return true;
  }
  std::shared_ptr<rss::service::DeferredSessionCompletion> defer() override {
    throw std::logic_error("chat benchmark must not defer");
  }
  std::vector<rss::service::OutboundMessage> messages;
};

rss::protocol::Packet decodeMessage(
    const rss::service::OutboundMessage& message) {
  if (message.kind != rss::service::OutboundMessageKind::SendBytes) {
    throw std::runtime_error("benchmark received a disconnect command");
  }
  rss::protocol::PacketCodec codec;
  codec.feed(message.bytes.data(), message.bytes.size());
  const auto packets = codec.drainPackets();
  if (packets.size() != 1) {
    throw std::runtime_error(
        "benchmark expected exactly one packet per message");
  }
  return packets.front();
}

class MessageRouterFixture : public benchmark::Fixture {
 public:
  void SetUp(const benchmark::State& state) override {
    setup_error_.clear();
    router_.reset();
    service_ = std::make_unique<rss::service::RoomService>();
    router_ = std::make_unique<rss::service::MessageRouter>(*service_, users_);

    if (!prepareSession(1)) {
      return;
    }
    const auto created = service_->createRoom(1, "benchmark-room");
    if (!created.ok) {
      setup_error_ = "failed to create benchmark room";
      return;
    }

    for (std::int64_t i = 2; i <= state.range(0); ++i) {
      const auto session_id = static_cast<std::uint64_t>(i);
      if (!prepareSession(session_id)) {
        return;
      }
      if (!service_->joinRoom(session_id, created.room_id).ok) {
        setup_error_ = "failed to join benchmark room";
        return;
      }
    }

    const std::string message = "benchmark-message";
    chat_event_ = rss::service::SessionEvent{
        rss::service::SessionEventKind::Packet,
        1,
        rss::protocol::Packet{
            rss::protocol::PacketType::ChatReq,
            std::vector<std::uint8_t>(message.begin(), message.end()),
        },
        0,
        {},
    };
    try {
      RecordingSink sink;
      router_->handle(chat_event_, sink);
      if (sink.messages.size() != static_cast<std::size_t>(state.range(0))) {
        throw std::runtime_error(
            "chat benchmark produced an incorrect broadcast count");
      }
      std::set<std::uint64_t> recipients;
      for (const auto& output : sink.messages) {
        const auto packet = decodeMessage(output);
        const auto payload = rss::protocol::StructuredPayload::parse(
            rss::protocol::payloadToString(packet));
        if (packet.type != rss::protocol::PacketType::RoomBroadcast ||
            payload.requireField("event") != "CHAT" ||
            payload.requireField("message") != message ||
            output.session_id < 1 ||
            output.session_id > static_cast<std::uint64_t>(state.range(0)) ||
            !recipients.insert(output.session_id).second) {
          throw std::runtime_error(
              "chat benchmark did not produce expected room broadcasts");
        }
      }
    } catch (const std::exception& error) {
      setup_error_ = error.what();
    }
  }

 protected:
  bool prepareSession(std::uint64_t session_id) {
    try {
      const auto request = rss::protocol::encodeVersionRequest(
          {rss::protocol::kMinProtocolVersion,
           rss::protocol::kMaxProtocolVersion});
      RecordingSink sink;
      router_->handle({rss::service::SessionEventKind::Packet,
                       session_id,
                       {rss::protocol::PacketType::VersionReq,
                        {request.begin(), request.end()}},
                       0,
                       {}},
                      sink);
      if (sink.messages.size() != 1 ||
          sink.messages.front().session_id != session_id) {
        throw std::runtime_error(
            "benchmark version negotiation response missing");
      }
      const auto response = decodeMessage(sink.messages.front());
      if (response.type != rss::protocol::PacketType::VersionRes ||
          rss::protocol::decodeVersionResponse(rss::protocol::payloadToString(
              response)) != rss::protocol::kMaxProtocolVersion) {
        throw std::runtime_error("benchmark version negotiation failed");
      }
      if (!service_->attachUser(session_id, userRecord(session_id)).ok) {
        throw std::runtime_error("failed to attach benchmark user");
      }
      return true;
    } catch (const std::exception& error) {
      setup_error_ = error.what();
      return false;
    }
  }

  static rss::persistence::UserRecord userRecord(std::uint64_t id) {
    const auto original_id = id;
    rss::domain::UserId::Bytes bytes{};
    for (std::size_t index = 0; index < sizeof(id); ++index) {
      bytes[bytes.size() - 1 - index] = static_cast<std::uint8_t>(id & 0xffU);
      id >>= 8U;
    }
    const auto name = "user-" + std::to_string(original_id);
    return {rss::domain::UserId{bytes}, name, name};
  }

  rss::persistence::InMemoryUserRepository users_;
  std::unique_ptr<rss::service::RoomService> service_;
  std::unique_ptr<rss::service::MessageRouter> router_;
  rss::service::SessionEvent chat_event_;
  std::string setup_error_;
};

BENCHMARK_DEFINE_F(MessageRouterFixture, ChatFanout)
(benchmark::State& state) {
  if (!setup_error_.empty()) {
    state.SkipWithError(setup_error_);
    return;
  }

  CountingSink sink;
  for (auto _ : state) {
    router_->handle(chat_event_, sink);
    benchmark::DoNotOptimize(sink.count());
  }

  const auto expected = state.iterations() * state.range(0);
  if (sink.count() != static_cast<std::size_t>(expected)) {
    state.SkipWithError(
        "chat benchmark broadcast count changed during measurement");
    return;
  }
  state.SetItemsProcessed(expected);
}

BENCHMARK_REGISTER_F(MessageRouterFixture, ChatFanout)
    ->Arg(1)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000);

}  // namespace
