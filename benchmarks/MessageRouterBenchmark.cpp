#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "rss/persistence/InMemoryUserRepository.h"
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

class MessageRouterFixture : public benchmark::Fixture {
 public:
  void SetUp(const benchmark::State& state) override {
    setup_error_.clear();
    router_.reset();
    service_ = std::make_unique<rss::service::RoomService>();
    router_ = std::make_unique<rss::service::MessageRouter>(*service_, users_);

    service_->attachUser(1, userRecord(1));
    const auto created = service_->createRoom(1, "benchmark-room");
    if (!created.ok) {
      setup_error_ = "failed to create benchmark room";
      return;
    }

    for (std::int64_t i = 2; i <= state.range(0); ++i) {
      const auto session_id = static_cast<std::uint64_t>(i);
      service_->attachUser(session_id, userRecord(session_id));
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
  }

 protected:
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

  for (auto _ : state) {
    CountingSink sink;
    router_->handle(chat_event_, sink);
    benchmark::DoNotOptimize(sink.count());
  }

  state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK_REGISTER_F(MessageRouterFixture, ChatFanout)
    ->Arg(1)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000);

}  // namespace
