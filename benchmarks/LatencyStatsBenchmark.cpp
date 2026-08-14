#include <benchmark/benchmark.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "rss/tools/LatencyStats.h"

namespace {

void BM_LatencyReport(benchmark::State& state) {
  std::vector<std::chrono::microseconds> samples;
  samples.reserve(static_cast<std::size_t>(state.range(0)));
  for (std::int64_t i = 0; i < state.range(0); ++i) {
    samples.emplace_back((i * 37) % 10'000);
  }

  for (auto _ : state) {
    auto report = rss::tools::latencyReport(samples);
    benchmark::DoNotOptimize(report);
  }

  state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_LatencyReport)->Arg(100)->Arg(1000)->Arg(10'000);

}  // namespace
