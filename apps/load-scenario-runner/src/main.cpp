#include <iostream>
#include <string_view>
#include <vector>

#include "Program.h"

int main(int argc, char* argv[]) {
  std::vector<std::string_view> args;
  args.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
  for (int index = 1; index < argc; ++index) {
    args.emplace_back(argv[index]);
  }
  return rss::tools::runScenarioProgram(args, std::cout, std::cerr);
}
