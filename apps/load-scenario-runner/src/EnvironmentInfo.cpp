#include "EnvironmentInfo.h"

#include <sys/utsname.h>

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#ifndef RSS_GIT_COMMIT
#define RSS_GIT_COMMIT "unknown"
#endif

#ifndef RSS_COMPILER
#define RSS_COMPILER "unknown"
#endif

#ifndef RSS_BUILD_TYPE
#define RSS_BUILD_TYPE "unknown"
#endif

namespace rss::tools {
namespace {

std::string cpuModel() {
  std::ifstream cpu_info{"/proc/cpuinfo"};
  std::string hardware;
  std::string processor;
  std::string implementer;
  std::string architecture;
  std::string part;
  std::string line;
  while (std::getline(cpu_info, line)) {
    const auto separator = line.find(':');
    if (separator == std::string::npos) {
      continue;
    }

    auto value = line.substr(separator + 1);
    const auto first = value.find_first_not_of(" \t");
    if (first != std::string::npos) {
      value.erase(0, first);
    }
    if (value.empty()) {
      continue;
    }

    const auto field = std::string_view{line}.substr(0, separator);
    if (field.starts_with("model name")) {
      return value;
    }
    if (field.starts_with("Hardware")) {
      hardware = value;
    } else if (field.starts_with("Processor")) {
      processor = value;
    } else if (field.starts_with("CPU implementer")) {
      implementer = value;
    } else if (field.starts_with("CPU architecture")) {
      architecture = value;
    } else if (field.starts_with("CPU part")) {
      part = value;
    }
  }

  if (!hardware.empty()) {
    return hardware;
  }
  if (!processor.empty()) {
    return processor;
  }
  if (!implementer.empty() || !architecture.empty() || !part.empty()) {
    std::ostringstream identity;
    identity << "implementer_"
             << (implementer.empty() ? "unknown" : implementer)
             << "_architecture_"
             << (architecture.empty() ? "unknown" : architecture) << "_part_"
             << (part.empty() ? "unknown" : part);
    return identity.str();
  }
  return "unknown";
}

std::string safeValue(std::string value) {
  if (value.empty()) {
    return "unknown";
  }
  for (auto& character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (std::isspace(byte) != 0 || character == '=') {
      character = '_';
    }
  }
  return value;
}

}  // namespace

EnvironmentInfo collectEnvironmentInfo(
    std::size_t workers, int requested_slow_receive_buffer_bytes) {
  std::string os{"unknown"};
  std::string kernel{"unknown"};
  utsname system_info{};
  if (uname(&system_info) == 0) {
    os = system_info.sysname;
    kernel = system_info.release;
  }

  return EnvironmentInfo{
      .commit = RSS_GIT_COMMIT,
      .os = std::move(os),
      .kernel = std::move(kernel),
      .cpu = cpuModel(),
      .compiler = RSS_COMPILER,
      .build_type = RSS_BUILD_TYPE,
      .workers = workers,
      .requested_slow_receive_buffer_bytes =
          requested_slow_receive_buffer_bytes,
  };
}

std::string formatEnvironment(const EnvironmentInfo& info) {
  std::ostringstream output;
  output << "environment commit=" << safeValue(info.commit)
         << " os=" << safeValue(info.os) << " kernel=" << safeValue(info.kernel)
         << " cpu=" << safeValue(info.cpu)
         << " compiler=" << safeValue(info.compiler)
         << " build_type=" << safeValue(info.build_type)
         << " workers=" << info.workers
         << " requested_slow_receive_buffer_bytes="
         << info.requested_slow_receive_buffer_bytes;
  return output.str();
}

}  // namespace rss::tools
