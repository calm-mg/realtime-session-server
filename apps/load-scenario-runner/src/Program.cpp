#include "Program.h"

#include <exception>
#include <iostream>
#include <ostream>
#include <sstream>
#include <stdexcept>

#include "EnvironmentInfo.h"
#include "ScenarioRunner.h"

namespace rss::tools {
namespace {

constexpr std::string_view kUsage =
    "Usage: rss_load_scenario_runner "
    "[--scenario <broadcast|multi-room|slow-client>] "
    "[--clients N] [--rooms N] [--messages N] [--payload-bytes N] "
    "[--slow-clients N] [--repeat N] [--workers N]";

class ScopedStandardOutputSilencer {
 public:
  ScopedStandardOutputSilencer() : previous_(std::cout.rdbuf(sink_.rdbuf())) {}
  ~ScopedStandardOutputSilencer() { std::cout.rdbuf(previous_); }

  ScopedStandardOutputSilencer(const ScopedStandardOutputSilencer&) = delete;
  ScopedStandardOutputSilencer& operator=(const ScopedStandardOutputSilencer&) =
      delete;

 private:
  std::ostringstream sink_;
  std::streambuf* previous_;
};

ScenarioRunResult runQuietly(const internal::RunScenarioOnce& run_once,
                             const ScenarioOptions& options,
                             std::size_t run_id) {
  ScopedStandardOutputSilencer silence_output;
  return run_once(options, run_id);
}

}  // namespace

namespace internal {

int runScenarioProgramWith(std::span<const std::string_view> args,
                           std::ostream& out, std::ostream& err,
                           const RunScenarioOnce& run_once) {
  ScenarioOptions options;
  try {
    options = parseScenarioOptions(args);
  } catch (const std::invalid_argument& exception) {
    err << "argument error: " << exception.what() << '\n' << kUsage << '\n';
    return 2;
  }

  try {
    const ScenarioTuning tuning;
    out << formatEnvironment(collectEnvironmentInfo(
               options.worker_count, tuning.socket_receive_buffer_bytes))
        << '\n';
    static_cast<void>(runQuietly(run_once, options, 0));

    bool all_successful = true;
    for (std::size_t run = 1; run <= options.repeats; ++run) {
      const auto result = runQuietly(run_once, options, run);
      out << formatRunResult(run, options.scenario, options, result) << '\n';
      if (!isSuccessful(options.scenario, result, options.slow_clients)) {
        all_successful = false;
      }
    }
    return all_successful ? 0 : 1;
  } catch (const std::exception& exception) {
    err << "execution error: " << exception.what() << '\n';
  } catch (...) {
    err << "execution error: unknown exception\n";
  }
  return 3;
}

}  // namespace internal

int runScenarioProgram(std::span<const std::string_view> args,
                       std::ostream& out, std::ostream& err) {
  const ScenarioTuning tuning;
  const auto run_once = [tuning](const ScenarioOptions& options,
                                 std::size_t run_id) {
    return ScenarioRunner{tuning}.runOnce(options, run_id);
  };
  return internal::runScenarioProgramWith(args, out, err, run_once);
}

}  // namespace rss::tools
