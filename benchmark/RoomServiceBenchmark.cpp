#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>

#include "rss/service/RoomService.h"

namespace {

class RoomServiceFixture : public benchmark::Fixture {
 public:
  void SetUp(const benchmark::State& state) override {
    service_.login(1, "user-1");
    const auto created = service_.createRoom(1, "benchmark-room");
    if (!created.ok) {
      setup_error_ = "failed to create benchmark room";
      return;
    }

    for (std::int64_t i = 2; i <= state.range(0); ++i) {
      const auto session_id = static_cast<std::uint64_t>(i);
      service_.login(session_id, "user-" + std::to_string(i));
      if (!service_.joinRoom(session_id, created.room_id).ok) {
        setup_error_ = "failed to join benchmark room";
        return;
      }
    }
  }

 protected:
  rss::service::RoomService service_;
  std::string setup_error_;
};

BENCHMARK_DEFINE_F(RoomServiceFixture, ChatFanout)
(benchmark::State& state) {
  if (!setup_error_.empty()) {
    state.SkipWithError(setup_error_);
    return;
  }

  for (auto _ : state) {
    auto result = service_.chat(1);
    benchmark::DoNotOptimize(result.recipients);
  }

  state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK_REGISTER_F(RoomServiceFixture, ChatFanout)
    ->Arg(1)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000);

}  // namespace
