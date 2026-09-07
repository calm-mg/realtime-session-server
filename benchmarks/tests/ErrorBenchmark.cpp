#include <benchmark/benchmark.h>

namespace {
void BM_IntentionalFailure(benchmark::State& state) {
  state.SkipWithError(
      "intentional failure to verify the benchmark exit status");
}
BENCHMARK(BM_IntentionalFailure);

void BM_MixedRepetitions(benchmark::State& state) {
  static int invocations = 0;
  ++invocations;
  for (auto _ : state) {
    benchmark::DoNotOptimize(invocations);
  }
  if (invocations == 3) {
    state.SkipWithError("intentional failure in the third repetition");
  }
}
BENCHMARK(BM_MixedRepetitions)->Iterations(1)->Repetitions(3);
}  // namespace
