#include <gtest/gtest.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

struct ProcessResult {
  int exit_code{};
  std::string standard_error;
};

ProcessResult runServerWith(const char* port, const char* worker_count) {
  std::array<int, 2> error_pipe{};
  if (::pipe(error_pipe.data()) != 0) {
    throw std::runtime_error("pipe failed");
  }

  const auto pid = ::fork();
  if (pid < 0) {
    ::close(error_pipe[0]);
    ::close(error_pipe[1]);
    throw std::runtime_error("fork failed");
  }
  if (pid == 0) {
    ::close(error_pipe[0]);
    if (::dup2(error_pipe[1], STDERR_FILENO) < 0) {
      ::_exit(126);
    }
    ::close(error_pipe[1]);
    ::execl(RSS_SERVER_EXECUTABLE_PATH, RSS_SERVER_EXECUTABLE_PATH, "127.0.0.1",
            port, worker_count, nullptr);
    ::_exit(127);
  }

  ::close(error_pipe[1]);
  std::string error_output;
  std::array<char, 1024> buffer{};
  for (;;) {
    const auto read_count = ::read(error_pipe[0], buffer.data(), buffer.size());
    if (read_count > 0) {
      error_output.append(buffer.data(), static_cast<std::size_t>(read_count));
      continue;
    }
    if (read_count < 0 && errno == EINTR) {
      continue;
    }
    break;
  }
  ::close(error_pipe[0]);

  int status{};
  if (::waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) {
    throw std::runtime_error("server process did not exit normally");
  }
  return {.exit_code = WEXITSTATUS(status),
          .standard_error = std::move(error_output)};
}

void expectStructuredFailure(const ProcessResult& result) {
  EXPECT_NE(result.exit_code, EXIT_SUCCESS);
  EXPECT_TRUE(result.standard_error.starts_with("{"));
  EXPECT_TRUE(result.standard_error.ends_with("}\n"));
  EXPECT_EQ(
      static_cast<std::size_t>(std::count(result.standard_error.begin(),
                                          result.standard_error.end(), '\n')),
      1U);
  EXPECT_NE(result.standard_error.find("\"level\":\"error\""),
            std::string::npos);
  EXPECT_NE(result.standard_error.find("\"event\":\"server_failed\""),
            std::string::npos);
}

TEST(ServerProgramTest, InvalidPortProducesStructuredFailure) {
  expectStructuredFailure(runServerWith("invalid", "1"));
}

TEST(ServerProgramTest, InvalidWorkerCountProducesStructuredFailure) {
  expectStructuredFailure(runServerWith("7777", "invalid"));
}

}  // namespace
