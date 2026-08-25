# 운영 종료 신호와 정상 종료 연결 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Linux `rss_server`가 `SIGINT`와 `SIGTERM`을 전용 `sigwait()` 스레드에서 받아 기존 `TcpServer::stop()` drain 절차로 정상 종료되게 합니다.

**Architecture:** 애플리케이션 계층에서 두 종료 신호를 worker 생성 전에 차단하고, `ShutdownSignalMonitor`가 전용 스레드에서 동기적으로 기다립니다. monitor는 신호를 받으면 `TcpServer::stop()`만 호출하며, 네트워크 라이브러리와 embedded server의 동작은 변경하지 않습니다. 실제 `rss_server` 자식 프로세스 통합 테스트가 두 신호의 exit status와 종료 상한을 검증합니다.

**Tech Stack:** C++20, POSIX Threads와 signals (`pthread_sigmask`, `sigwait`, `pthread_kill`), Linux sockets/process API, CMake, GoogleTest

**Spec:** `docs/design/2026-08-25-graceful-signal-shutdown.md`

## Global Constraints

- 전체 서버와 이 변경의 테스트는 Linux에서만 빌드합니다.
- 소켓과 `epoll` 상태는 기존처럼 I/O 스레드만 변경합니다.
- signal waiter는 `TcpServer::stop()`만 호출하고 소켓을 직접 조작하지 않습니다.
- `server-core`와 `server-net-linux`의 public API 및 embedded server 동작은 변경하지 않습니다.
- 두 번째 종료 신호는 강제 종료로 승격하지 않으며 기존 `graceful_shutdown_timeout`만 강제 종료 시점을 결정합니다.
- `SIGINT`와 `SIGTERM`의 signal mask는 서버 프로세스가 끝날 때까지 복원하지 않습니다.
- 프로토콜과 Qt 클라이언트 소스는 변경하지 않습니다.
- 새 코드와 문서는 프로젝트의 `rss/...` include, 경고 옵션과 한글 문서 규칙을 따릅니다.

---

### Task 1: 운영 종료 신호를 기존 drain 경로에 연결

**Files:**
- Create: `tests/server-net-linux/ServerSignalTest.cpp`
- Create: `apps/server/src/ShutdownSignalMonitor.h`
- Create: `apps/server/src/ShutdownSignalMonitor.cpp`
- Modify: `apps/server/src/main.cpp`
- Modify: `apps/server/CMakeLists.txt`
- Modify: `tests/server-net-linux/CMakeLists.txt`

**Interfaces:**
- Consumes: `rss_server <host> <port> <worker-count>` CLI와 CMake target `rss_server`
- Produces: `ServerSignalTest.StopsThroughGracefulPath/{Sigint,Sigterm}` GoogleTest cases
- Produces: `sigset_t rss::server::blockShutdownSignals()`
- Produces: `rss::server::ShutdownSignalMonitor(sigset_t, rss::net::TcpServer&)`
- Produces: `void ShutdownSignalMonitor::throwIfFailed() const`

- [ ] **Step 1: 실제 서버 프로세스를 관리하는 테스트 helper 작성**

`tests/server-net-linux/ServerSignalTest.cpp`를 만들고 다음 Linux 전용 helper를
anonymous namespace에 정의합니다. 자식은 실제 `rss_server`를 `exec`하고,
부모는 loopback listener 준비를 확인합니다. RAII destructor는 실패한 테스트가
자식 프로세스를 남기지 않도록 아직 실행 중인 자식만 `SIGKILL` 후
`waitpid()`로 회수합니다.

```cpp
#include <arpa/inet.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

std::uint16_t reserveLoopbackPort() {
  const auto fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    throw std::runtime_error("socket failed");
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(0);
  if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    ::close(fd);
    throw std::runtime_error("bind failed");
  }

  socklen_t size = sizeof(address);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) < 0) {
    ::close(fd);
    throw std::runtime_error("getsockname failed");
  }
  const auto port = ntohs(address.sin_port);
  ::close(fd);
  return port;
}

bool canConnect(std::uint16_t port) {
  const auto fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return false;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  const auto connected =
      ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
  ::close(fd);
  return connected;
}

struct ExitResult {
  bool exited{};
  int status{};
};

class ServerProcess {
 public:
  explicit ServerProcess(std::uint16_t port) : port_(port) {
    pid_ = ::fork();
    if (pid_ < 0) {
      throw std::runtime_error("fork failed");
    }
    if (pid_ == 0) {
      const auto port_text = std::to_string(port_);
      ::execl(RSS_SERVER_EXECUTABLE_PATH, RSS_SERVER_EXECUTABLE_PATH,
              "127.0.0.1", port_text.c_str(), "1", nullptr);
      ::_exit(127);
    }
  }

  ~ServerProcess() {
    if (pid_ <= 0) {
      return;
    }
    const auto wait_result = ::waitpid(pid_, nullptr, WNOHANG);
    if (wait_result == 0) {
      static_cast<void>(::kill(pid_, SIGKILL));
      static_cast<void>(::waitpid(pid_, nullptr, 0));
    }
  }

  ServerProcess(const ServerProcess&) = delete;
  ServerProcess& operator=(const ServerProcess&) = delete;

  bool waitUntilListening(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (canConnect(port_)) {
        return true;
      }
      int status{};
      if (::waitpid(pid_, &status, WNOHANG) == pid_) {
        pid_ = -1;
        return false;
      }
      std::this_thread::sleep_for(10ms);
    }
    return false;
  }

  bool sendSignal(int signal_number) const {
    return ::kill(pid_, signal_number) == 0;
  }

  ExitResult waitForExit(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      int status{};
      const auto result = ::waitpid(pid_, &status, WNOHANG);
      if (result == pid_) {
        pid_ = -1;
        return {.exited = true, .status = status};
      }
      if (result < 0 && errno != EINTR) {
        throw std::runtime_error(std::string("waitpid failed: ") +
                                 std::strerror(errno));
      }
      std::this_thread::sleep_for(10ms);
    }
    return {};
  }

 private:
  pid_t pid_{-1};
  std::uint16_t port_{};
};
```

- [ ] **Step 2: `SIGINT`와 `SIGTERM` parameterized test 작성**

같은 파일의 anonymous namespace 안에 다음 test fixture와 case를 추가합니다.
listener 준비 상한은 2초, 종료 상한은 기본 drain timeout 5초에 정리 여유를
더한 7초로 고정합니다.

```cpp
class ServerSignalTest : public testing::TestWithParam<int> {};

TEST_P(ServerSignalTest, StopsThroughGracefulPath) {
  ServerProcess server(reserveLoopbackPort());
  ASSERT_TRUE(server.waitUntilListening(2s));
  ASSERT_TRUE(server.sendSignal(GetParam()));

  const auto result = server.waitForExit(7s);
  ASSERT_TRUE(result.exited);
  ASSERT_TRUE(WIFEXITED(result.status));
  EXPECT_EQ(WEXITSTATUS(result.status), EXIT_SUCCESS);
}

INSTANTIATE_TEST_SUITE_P(
    TerminationSignals, ServerSignalTest, testing::Values(SIGINT, SIGTERM),
    [](const testing::TestParamInfo<int>& info) {
      return info.param == SIGINT ? "Sigint" : "Sigterm";
    });

}  // namespace
```

테스트가 listener 준비 전에 끝난 경우에는 `waitUntilListening()` 실패로
구분하고, 종료 후에는 `WIFEXITED`를 먼저 검사하므로 기본 signal disposition에
의한 `WIFSIGNALED` 종료를 정상 성공으로 오인하지 않습니다.

- [ ] **Step 3: 테스트 target에 소스와 실제 서버 경로 연결**

`tests/server-net-linux/CMakeLists.txt`의 `rss_server_net_tests` source 목록에
`ServerSignalTest.cpp`를 추가하고 target 선언 뒤 다음 설정을 추가합니다.

```cmake
add_dependencies(rss_server_net_tests rss_server)
target_compile_definitions(rss_server_net_tests
    PRIVATE
        RSS_SERVER_EXECUTABLE_PATH="$<TARGET_FILE:rss_server>"
)
```

- [ ] **Step 4: 실패하는 회귀 테스트 확인**

Run:

```bash
cmake --preset linux-dev
cmake --build --preset linux-dev --target rss_server rss_server_net_tests
ctest --test-dir build/linux-dev --output-on-failure \
  -R 'ServerSignalTest.*StopsThroughGracefulPath'
```

Expected: 두 case 모두 자식이 `SIGINT` 또는 `SIGTERM`에 의해 종료되어
`WIFEXITED(result.status)` assertion이 실패합니다.

- [ ] **Step 5: 종료 신호 차단과 monitor 인터페이스 선언**

`apps/server/src/ShutdownSignalMonitor.h`를 다음과 같이 작성합니다.

```cpp
#pragma once

#include <signal.h>

#include <atomic>
#include <thread>

#include "rss/net/TcpServer.h"

namespace rss::server {

[[nodiscard]] sigset_t blockShutdownSignals();

class ShutdownSignalMonitor {
 public:
  ShutdownSignalMonitor(sigset_t signals, net::TcpServer& server);
  ~ShutdownSignalMonitor();

  ShutdownSignalMonitor(const ShutdownSignalMonitor&) = delete;
  ShutdownSignalMonitor& operator=(const ShutdownSignalMonitor&) = delete;

  void throwIfFailed() const;

 private:
  void waitLoop();

  sigset_t signals_{};
  net::TcpServer& server_;
  std::atomic<bool> stopping_{false};
  std::atomic<int> wait_error_{0};
  std::thread thread_;
};

}  // namespace rss::server
```

- [ ] **Step 6: signal mask와 대기 스레드 구현**

`apps/server/src/ShutdownSignalMonitor.cpp`를 다음과 같이 작성합니다.
`pthread_sigmask()`와 `sigwait()`은 실패 시 `errno`가 아니라 반환값 자체가
오류 번호이므로 반드시 그 값을 메시지와 `wait_error_`에 사용합니다.

```cpp
#include "ShutdownSignalMonitor.h"

#include <pthread.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace rss::server {
namespace {

std::runtime_error signalError(const char* operation, int error) {
  return std::runtime_error(std::string(operation) + ": " +
                            std::strerror(error));
}

}  // namespace

sigset_t blockShutdownSignals() {
  sigset_t signals{};
  if (::sigemptyset(&signals) != 0 || ::sigaddset(&signals, SIGINT) != 0 ||
      ::sigaddset(&signals, SIGTERM) != 0) {
    throw signalError("failed to build shutdown signal set", errno);
  }

  const auto result = ::pthread_sigmask(SIG_BLOCK, &signals, nullptr);
  if (result != 0) {
    throw signalError("pthread_sigmask failed", result);
  }
  return signals;
}

ShutdownSignalMonitor::ShutdownSignalMonitor(sigset_t signals,
                                             net::TcpServer& server)
    : signals_(signals), server_(server), thread_([this] { waitLoop(); }) {}

ShutdownSignalMonitor::~ShutdownSignalMonitor() {
  stopping_.store(true, std::memory_order_release);
  if (!thread_.joinable()) {
    return;
  }
  static_cast<void>(::pthread_kill(thread_.native_handle(), SIGTERM));
  thread_.join();
}

void ShutdownSignalMonitor::throwIfFailed() const {
  const auto error = wait_error_.load(std::memory_order_acquire);
  if (error != 0) {
    throw signalError("sigwait failed", error);
  }
}

void ShutdownSignalMonitor::waitLoop() {
  while (!stopping_.load(std::memory_order_acquire)) {
    int signal_number{};
    const auto result = ::sigwait(&signals_, &signal_number);
    if (result != 0) {
      wait_error_.store(result, std::memory_order_release);
      server_.stop();
      return;
    }
    if (stopping_.load(std::memory_order_acquire)) {
      return;
    }
    server_.stop();
  }
}

}  // namespace rss::server
```

`signal_number`는 `sigwait()`의 유효 출력 인자이며 두 신호를 동일하게
처리하므로 별도 분기에는 사용하지 않습니다.

- [ ] **Step 7: monitor를 서버 프로세스 수명에 연결**

`apps/server/src/main.cpp`에 다음 include를 추가합니다.

```cpp
#include "ShutdownSignalMonitor.h"
```

기존 `try` 블록의 server 생성과 실행을 다음 순서로 변경합니다. signal은
`TcpServer::run()`이 worker 스레드를 만들기 전에 차단되며, 지역 변수 역순
파괴에 따라 monitor가 server보다 먼저 join됩니다.

```cpp
  try {
    const auto shutdown_signals = rss::server::blockShutdownSignals();
    rss::net::TcpServer server(config);
    rss::server::ShutdownSignalMonitor signal_monitor(shutdown_signals, server);
    server.run();
    signal_monitor.throwIfFailed();
  } catch (const std::exception& ex) {
```

- [ ] **Step 8: executable에 monitor 구현 추가**

`apps/server/CMakeLists.txt`의 executable source를 다음처럼 확장합니다.

```cmake
add_executable(rss_server
    src/main.cpp
    src/ShutdownSignalMonitor.cpp
)
```

별도 public library나 include directory는 만들지 않습니다. 헤더와 사용처가
같은 `src` 디렉터리에 있으므로 quoted include로 찾을 수 있고, Linux 전용
`rss_server` target 밖으로 signal 정책이 노출되지 않습니다.

- [ ] **Step 9: 종료 신호 회귀 테스트 통과 확인**

Run:

```bash
cmake --build --preset linux-dev --target rss_server rss_server_net_tests
ctest --test-dir build/linux-dev --output-on-failure \
  -R 'ServerSignalTest.*StopsThroughGracefulPath'
```

Expected: `Sigint`와 `Sigterm` case가 모두 7초보다 빨리 exit code 0으로 PASS.

- [ ] **Step 10: 기존 종료 상태 머신 회귀 테스트 확인**

Run:

```bash
ctest --test-dir build/linux-dev --output-on-failure \
  -R 'TcpServerBackpressureTest|EmbeddedServerTest'
```

Expected: 기존 명시적 `stop()`, drain과 embedded server 관련 case 모두 PASS.

- [ ] **Step 11: 코드 변경 커밋**

```bash
git add apps/server/src/ShutdownSignalMonitor.h \
  apps/server/src/ShutdownSignalMonitor.cpp \
  apps/server/src/main.cpp apps/server/CMakeLists.txt \
  tests/server-net-linux/ServerSignalTest.cpp \
  tests/server-net-linux/CMakeLists.txt
git commit -m "기능: 운영 종료 신호를 정상 종료에 연결"
```

---

### Task 2: 운영 종료 동작과 작업 상태 문서화

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture.md`
- Modify: `docs/known-issues.md`
- Modify: `docs/project-status.md`

**Interfaces:**
- Consumes: 구현된 `SIGINT`·`SIGTERM` → `TcpServer::stop()` 경로
- Produces: 운영자 실행 절차, 서버 종료 구조 설명과 갱신된 작업 우선순위

- [ ] **Step 1: README에 운영 종료 명령과 의미 추가**

`README.md`의 “3. 서버 실행”에서 기본 인자 설명 다음에 아래 내용을
추가합니다.

```markdown
### 서버 종료

포그라운드에서 실행 중인 서버는 `Ctrl+C`로 종료할 수 있습니다. 다른
프로세스에서는 서버 PID에 `SIGTERM`을 보냅니다.

```bash
kill -TERM <server-pid>
```

두 신호는 새 연결과 입력을 중단하고 이미 받은 요청과 미전송 응답을 비우는
정상 종료를 요청합니다. 기본 5초인 `graceful_shutdown_timeout` 안에 drain이
끝나지 않으면 서버는 남은 작업을 중단하고 네트워크 자원을 정리합니다.
```

- [ ] **Step 2: 서버 구조 문서의 종료 진입 경로 갱신**

`docs/architecture.md`의 “종료 순서” 마지막 두 문단을 다음 의미로
교체합니다.

```markdown
`TcpServer::stop()`이 호출되면 이 종료 절차가 시작됩니다. `rss_server` 실행
파일은 worker를 만들기 전에 `SIGINT`와 `SIGTERM`을 차단하고 전용
`sigwait()` 스레드에서 두 신호를 기다립니다. 신호를 받으면 이 스레드는
`stop()`만 호출하며, `eventfd` 알림을 받은 I/O 스레드가 기존 drain 상태
머신을 진행합니다.

프로세스 신호 정책은 서버 애플리케이션에만 있습니다. 부하 시나리오의
embedded server와 `TcpServer`를 직접 사용하는 코드는 기존처럼 `stop()`을
명시적으로 호출합니다.
```

- [ ] **Step 3: 해결된 알려진 문제 제거**

`docs/known-issues.md`의 “프로세스 종료 신호와 정상 종료 미연결” 제목부터 네
개의 완료 조건까지 제거합니다. “운영 안정성” 아래 첫 항목이
“`ServerConfig` 검증 범위 부족”이 되게 하며 해결 이력을 이 문서에 남기지
않습니다.

- [ ] **Step 4: 프로젝트 상태와 다음 우선순위 갱신**

`docs/project-status.md`에서 다음을 적용합니다.

- `마지막 갱신`을 `2026-08-25`로 변경
- “최근 완료” 첫 항목에 `2026-08-25: SIGINT와 SIGTERM을 기존 정상 종료 drain 경로에 연결` 추가
- “현재 단계”에서 종료 경로가 남았다는 표현을 제거하고 설정·예외 정책을
  먼저 보강한다고 기록
- “우선순위 1”에서 완료된 signal 항목 제거
- 남은 최우선 항목은 `ServerConfig` 전체 값 검증으로 유지

- [ ] **Step 5: 문서 일관성과 형식 확인**

Run:

```bash
rg -n "아직.*SIGINT|미연결|signal handler에서는" README.md docs
git diff --check
```

Expected: 과거 문제를 현재 상태로 설명하는 문구가 없고 `git diff --check`가
출력 없이 성공. 설계와 계획 문서에서 과거 문제를 설명하는 문맥은 검색
결과가 나올 수 있으므로 직접 확인해 유지합니다.

- [ ] **Step 6: 문서 변경 커밋**

```bash
git add README.md docs/architecture.md docs/known-issues.md \
  docs/project-status.md
git commit -m "문서: 운영 종료 신호 사용법과 상태 갱신"
```

---

### Task 3: Linux 전체 검증과 최종 변경 확인

**Files:**
- Verify only: 전체 변경 파일

**Interfaces:**
- Consumes: Tasks 1~2의 구현과 문서
- Produces: Linux build, test, format과 tidy 검증 결과

- [ ] **Step 1: Linux 개발 preset 새로 구성하고 전체 빌드**

```bash
cmake --preset linux-dev
cmake --build --preset linux-dev
```

Expected: `rss_server`, 모든 Linux 도구와 test target이 경고를 오류 없이
빌드됨.

- [ ] **Step 2: 전체 Linux 테스트 실행**

```bash
ctest --preset linux-dev
```

Expected: protocol, server-core, server-net-linux와 load-test-support를 포함한
전체 test PASS. `ServerSignalTest`의 두 case도 각각 exit code 0으로 PASS.

- [ ] **Step 3: format 검사 실행**

```bash
cmake --build build/linux-dev --target format-check
```

Expected: PASS. 실패하면 변경한 C++/CMake 파일에 formatter를 적용하고 다시
실행합니다.

- [ ] **Step 4: clang-tidy 검사 실행**

```bash
cmake --build build/linux-dev --target tidy-check
```

Expected: 환경에 `clang-tidy`가 있으면 PASS. 도구가 없어 실행할 수 없으면
누락 이유와 나머지 검증 결과를 최종 보고에 기록합니다.

- [ ] **Step 5: 공개 범위와 작업 트리 최종 확인**

```bash
git status --short
git diff main...HEAD --check
git diff main...HEAD --stat
git log --oneline main..HEAD
```

Expected: 계획된 서버 애플리케이션, Linux 테스트와 네 문서만 구현 변경에
포함됩니다. 기존 미추적 `.codex/`는 커밋되지 않으며, 프로토콜·Qt·플랫폼
독립 라이브러리 파일에는 변경이 없습니다.
