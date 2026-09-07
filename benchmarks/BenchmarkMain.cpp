#include <benchmark/benchmark.h>

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
bool explicitlyDisabled(std::string_view value) {
  std::string normalized(value);
  for (auto& character : normalized) {
    character =
        static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return normalized == "false" || normalized == "0" || normalized == "off" ||
         normalized == "no" || normalized == "f" || normalized == "n";
}

bool validateReportingOptions(int argc, char** argv) {
  struct Option {
    std::string_view argument;
    const char* environment;
  };
  const Option options[] = {{"--benchmark_report_aggregates_only",
                             "BENCHMARK_REPORT_AGGREGATES_ONLY"},
                            {"--benchmark_display_aggregates_only",
                             "BENCHMARK_DISPLAY_AGGREGATES_ONLY"}};
  for (const auto& option : options) {
    const auto reject = [](std::string_view name) {
      std::cerr
          << name
          << ": aggregate-only reporting is unsupported because it can hide "
             "failed repetitions; remove this option or set it to false.\n";
      return false;
    };
    if (const auto* value = std::getenv(option.environment);
        value != nullptr && !explicitlyDisabled(value)) {
      return reject(option.environment);
    }
    const auto prefix = std::string(option.argument) + "=";
    for (int index = 1; index < argc; ++index) {
      const std::string_view argument(argv[index]);
      if (argument == option.argument ||
          (argument.starts_with(prefix) &&
           !explicitlyDisabled(argument.substr(prefix.size())))) {
        return reject(option.argument);
      }
    }
  }
  return true;
}

class CheckedReporter final : public benchmark::BenchmarkReporter {
 public:
  CheckedReporter() : reporter_(benchmark::CreateDefaultDisplayReporter()) {}

  bool ReportContext(const Context& context) override {
    return reporter_->ReportContext(context);
  }
  void ReportRunsConfig(double min_time, bool explicit_iterations,
                        benchmark::IterationCount iterations) override {
    reporter_->ReportRunsConfig(min_time, explicit_iterations, iterations);
  }
  void ReportRuns(const std::vector<Run>& runs) override {
    for (const auto& run : runs) {
      failed_ = failed_ || run.skipped == benchmark::internal::SkippedWithError;
    }
    reporter_->ReportRuns(runs);
  }
  void Finalize() override { reporter_->Finalize(); }
  bool failed() const { return failed_; }

 private:
  std::unique_ptr<benchmark::BenchmarkReporter> reporter_;
  bool failed_{};
};
}  // namespace

int main(int argc, char** argv) {
  if (!validateReportingOptions(argc, argv)) {
    return 1;
  }
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  CheckedReporter reporter;
  benchmark::RunSpecifiedBenchmarks(&reporter);
  benchmark::Shutdown();
  return reporter.failed() ? 1 : 0;
}
