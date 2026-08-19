#pragma once

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <span>
#include <string_view>

#include "rss/tools/ScenarioOptions.h"
#include "rss/tools/ScenarioReport.h"

namespace rss::tools {

int runScenarioProgram(std::span<const std::string_view> args,
                       std::ostream& out, std::ostream& err);

namespace internal {

using RunScenarioOnce = std::function<ScenarioRunResult(
    const ScenarioOptions& options, std::size_t run_id)>;

int runScenarioProgramWith(std::span<const std::string_view> args,
                           std::ostream& out, std::ostream& err,
                           const RunScenarioOnce& run_once);

}  // namespace internal
}  // namespace rss::tools
