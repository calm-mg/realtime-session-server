#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>
#include <vector>

#include "rss/protocol/PacketCodec.h"
#include "rss/protocol/PacketTypes.h"

namespace {

void BM_PacketCodecEncode(benchmark::State& state) {
  const std::string payload(static_cast<std::size_t>(state.range(0)), 'x');

  for (auto _ : state) {
    auto bytes = rss::protocol::PacketCodec::encode(
        rss::protocol::PacketType::ChatReq, payload);
    benchmark::DoNotOptimize(bytes);
  }

  state.SetBytesProcessed(state.iterations() *
                          static_cast<std::int64_t>(payload.size()));
}

void BM_PacketCodecDecode(benchmark::State& state) {
  const std::string payload(static_cast<std::size_t>(state.range(0)), 'x');
  const auto bytes = rss::protocol::PacketCodec::encode(
      rss::protocol::PacketType::ChatReq, payload);

  for (auto _ : state) {
    rss::protocol::PacketCodec codec;
    codec.feed(bytes.data(), bytes.size());
    auto packet = codec.peekPacket();
    benchmark::DoNotOptimize(packet);
    codec.consumePacket();
  }

  state.SetBytesProcessed(state.iterations() *
                          static_cast<std::int64_t>(payload.size()));
}

void BM_PacketCodecDecodeBatch(benchmark::State& state) {
  const auto frame =
      rss::protocol::PacketCodec::encode(rss::protocol::PacketType::Ping, "x");
  std::vector<std::uint8_t> bytes;
  bytes.reserve(frame.size() * static_cast<std::size_t>(state.range(0)));
  for (std::int64_t index = 0; index < state.range(0); ++index) {
    bytes.insert(bytes.end(), frame.begin(), frame.end());
  }

  for (auto _ : state) {
    rss::protocol::PacketCodec codec;
    codec.feed(bytes.data(), bytes.size());
    for (std::int64_t index = 0; index < state.range(0); ++index) {
      auto packet = codec.peekPacket();
      benchmark::DoNotOptimize(packet);
      codec.consumePacket();
    }
  }

  state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_PacketCodecEncode)->Arg(0)->Arg(64)->Arg(512)->Arg(4092);
BENCHMARK(BM_PacketCodecDecode)->Arg(0)->Arg(64)->Arg(512)->Arg(4092);
BENCHMARK(BM_PacketCodecDecodeBatch)->Arg(16)->Arg(256)->Arg(4096);

}  // namespace
