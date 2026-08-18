#pragma once

#include <cstddef>
#include <string>

namespace rss::tools {

struct EnvironmentInfo {
  std::string commit;
  std::string os;
  std::string kernel;
  std::string cpu;
  std::string compiler;
  std::string build_type;
  std::size_t workers{};
  int requested_slow_receive_buffer_bytes{};
};

EnvironmentInfo collectEnvironmentInfo(std::size_t workers,
                                       int requested_slow_receive_buffer_bytes);
std::string formatEnvironment(const EnvironmentInfo& info);

}  // namespace rss::tools
