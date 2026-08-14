# 모노레포 구조 재편 구현 계획

**목표:** 기존 런타임 동작과 프로토콜을 바꾸지 않고 서버, 도구, 공용 라이브러리, 테스트, 벤치마크를 명확한 모노레포 경계로 재배치한다.

**아키텍처:** 루트 CMake는 공통 옵션과 하위 프로젝트 조합만 담당한다. 플랫폼 독립 `rss_protocol`과 `rss_server_core`를 아래 계층에 두고, Linux 전용 `rss_server_net`과 실행 파일이 그 위에 의존하도록 구성한다. 각 테스트 실행 파일은 자신이 검증하는 라이브러리에 직접 연결한다.

**기술 스택:** C++20, CMake 3.20+, Ninja/Make, POSIX Threads, GoogleTest, Google Benchmark, Linux epoll/eventfd, GitHub Actions

## 전체 제약 조건

- Qt 또는 GUI 코드를 추가하지 않는다.
- 패킷 프로토콜, payload 형식, 서버 동작, 제한값, 스레딩, 종료 처리를 변경하지 않는다.
- `rss` 네임스페이스와 기존 공개 클래스 및 include 표기 `rss/...`를 유지한다.
- 실행 파일 이름 `rss_server`, `rss_console_client`, `rss_load_test_client`를 유지한다.
- 기존 빌드 옵션 `RSS_BUILD_TESTS`, `RSS_BUILD_NETWORK_TARGETS`, `RSS_BUILD_BENCHMARKS`와 `CMAKE_EXPORT_COMPILE_COMMANDS` 설정을 유지한다.
- Linux 전용 소스와 애플리케이션은 `CMAKE_SYSTEM_NAME STREQUAL "Linux"`일 때만 구성한다.
- macOS와 Windows에서는 프로토콜, 서버 코어, 테스트, 벤치마크를 빌드할 수 있어야 한다.
- 문서와 `AGENTS.md`의 본문은 한글을 기본으로 하고 코드, 명령, 경로, API 이름은 영어를 유지한다.
- 공개 문서에는 비밀정보, 인증정보, 개인적 배경, 무관한 업무 맥락을 기록하지 않는다.
- 기계적인 파일 이동과 내용 변경을 구분하고 관련 없는 formatting 변경을 만들지 않는다.

---

## 파일 구조 지도

### 새로 만들 파일

- `AGENTS.md`: 저장소 구조, 의존성, 플랫폼, 검증, 공개 문서 규칙
- `CMakePresets.json`: `core-dev`, `linux-dev`, `release`, `benchmark` preset
- `apps/*/CMakeLists.txt`: 각 실행 파일 타깃
- `libs/*/CMakeLists.txt`: 공용 라이브러리 타깃과 직접 의존성
- `tests/CMakeLists.txt`: 테스트 하위 디렉터리 조합
- `tests/*/CMakeLists.txt`: 라이브러리별 GoogleTest 실행 파일
- `benchmarks/CMakeLists.txt`: 마이크로벤치마크 타깃

### 이동할 파일 묶음

- `include/rss/protocol/*`, `src/protocol/*` → `libs/protocol`
- `include/rss/domain/*`, `src/domain/*` → `libs/server-core`
- `include/rss/service/*`, `src/service/*` → `libs/server-core`
- 플랫폼 독립 `include/rss/net/*`, `src/net/{Session,WorkerPool}.cpp`, `include/rss/util/*` → `libs/server-core`
- Linux 전용 `EpollEventLoop`, `EventFdCompletionNotifier`, `TcpServer`, `net/detail` → `libs/server-net-linux`
- `include/rss/tools/LatencyStats.h` → `libs/load-test-support`
- `src/main.cpp`, `client/console_client.cpp`, `tools/load_test_client.cpp` → `apps/*/src/main.cpp`
- `test/*` → 담당 라이브러리 아래 `tests/*`
- `benchmark/*` → `benchmarks/*`

---

### Task 1: 기준선 고정과 저장소 지침 추가

**Files:**
- Create: `AGENTS.md`
- Verify: `CMakeLists.txt`, `README.md`, `CONTRIBUTING.md`

**Interfaces:**
- Consumes: 현재 `main`의 빌드 옵션과 문서화된 명령
- Produces: 이후 모든 작업이 따라야 할 한글 저장소 지침과 변경 전 테스트 기준선

- [ ] **Step 1: 변경 전 파일과 테스트 기준선을 기록한다**

Run:

```bash
git ls-files | sort > /tmp/rss-files-before.txt
cmake -S . -B build/refactor-baseline -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRSS_BUILD_NETWORK_TARGETS=OFF
cmake --build build/refactor-baseline --parallel
ctest --test-dir build/refactor-baseline -N
ctest --test-dir build/refactor-baseline --output-on-failure
```

Expected: macOS에서 configure와 build가 성공하고 기존 플랫폼 독립 테스트가 모두 통과한다. `ctest -N` 출력의 총 테스트 수를 작업 로그에 기록한다.

- [ ] **Step 2: 루트 에이전트 지침을 작성한다**

Create `AGENTS.md` with these exact sections and rules:

```markdown
# 저장소 작업 지침

## 프로젝트 개요

이 저장소는 C++20 실시간 세션 서버와 관련 클라이언트 및 도구를 함께 관리하는 모노레포다. Linux 전용 서버 네트워크 코드와 크로스플랫폼 프로토콜 및 서버 코어 코드를 명확히 분리한다.

## 디렉터리와 의존성

- `apps/`: 실행 파일 진입점과 애플리케이션별 조립 코드
- `libs/protocol`: 패킷 형식과 codec. 다른 프로젝트 라이브러리에 의존하지 않는다.
- `libs/server-core`: 플랫폼 독립 도메인, 서비스, session, worker, queue 코드. `protocol`에만 의존한다.
- `libs/server-net-linux`: epoll/eventfd/TCP 서버 코드. `server-core`와 `protocol`에 의존한다.
- `libs/load-test-support`: 부하 테스트 통계 지원 코드
- `tests/`: 대상 라이브러리별 테스트
- `benchmarks/`: 마이크로벤치마크

라이브러리는 `apps/`에 의존할 수 없고, 플랫폼 독립 라이브러리는 `server-net-linux`에 의존할 수 없다.

## 플랫폼 규칙

- 전체 서버와 POSIX 콘솔 도구는 Linux에서만 빌드한다.
- macOS와 Windows에서는 플랫폼 독립 라이브러리, 테스트, 벤치마크를 빌드한다.
- 소켓과 epoll 상태는 I/O 스레드만 변경한다.
- worker는 소켓을 직접 조작하지 않는다.

## 빌드와 검증

```bash
cmake --preset core-dev
cmake --build --preset core-dev
ctest --preset core-dev
```

Linux 전체 빌드는 `linux-dev`, 벤치마크는 `benchmark` preset을 사용한다. 변경 전 관련 테스트를 실행하고, 완료 전 build, test, `format-check`, 가능한 경우 `tidy-check`를 실행한다.

## 변경 규칙

- 프로토콜 변경 시 `docs/protocol.md`와 서버 및 클라이언트 테스트를 함께 수정한다.
- 실행 인자나 빌드 절차 변경 시 `README.md`와 `CONTRIBUTING.md`를 함께 수정한다.
- public include는 `rss/...` 표기를 유지한다.
- 관련 없는 리팩터링이나 formatting 변경을 같은 커밋에 섞지 않는다.

## 문서와 공개 저장소

- `AGENTS.md`와 프로젝트 문서의 본문은 한글을 기본으로 한다.
- 코드 식별자, API, 명령, 파일 및 디렉터리 이름은 영어를 유지할 수 있다.
- 비밀정보, 인증정보, 개인적 배경, 무관한 업무 맥락을 커밋하지 않는다.
```

- [ ] **Step 3: 지침 내용과 공개성 규칙을 검사한다**

Run:

```bash
rg -n -i 'password|secret|api[_-]?key|access[_-]?token' AGENTS.md
git diff --check
```

Expected: 인증정보로 보이는 값이 없고 공개 저장소 규칙만 포함되어 있다. `git diff --check`가 성공한다.

- [ ] **Step 4: 지침을 커밋한다**

```bash
git add AGENTS.md
git commit -m "docs: add repository agent guidance"
```

---

### Task 2: 프로토콜 라이브러리 분리

**Files:**
- Create: `libs/protocol/CMakeLists.txt`
- Move: `include/rss/protocol/Packet.h` → `libs/protocol/include/rss/protocol/Packet.h`
- Move: `include/rss/protocol/PacketCodec.h` → `libs/protocol/include/rss/protocol/PacketCodec.h`
- Move: `include/rss/protocol/PacketTypes.h` → `libs/protocol/include/rss/protocol/PacketTypes.h`
- Move: `src/protocol/PacketCodec.cpp` → `libs/protocol/src/PacketCodec.cpp`
- Move: `src/protocol/PacketTypes.cpp` → `libs/protocol/src/PacketTypes.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: 기존 `rss::protocol` 헤더와 구현
- Produces: `rss_protocol` CMake target, public include root `libs/protocol/include`

- [ ] **Step 1: 프로토콜 파일을 새 소유 위치로 이동한다**

Use rename-preserving file moves. Do not edit C++ content.

- [ ] **Step 2: 프로토콜 타깃을 정의한다**

Create `libs/protocol/CMakeLists.txt`:

```cmake
add_library(rss_protocol
    src/PacketCodec.cpp
    src/PacketTypes.cpp
)

target_include_directories(rss_protocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(rss_protocol PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_protocol)
```

- [ ] **Step 3: 공통 warning helper를 루트 CMake에 추가한다**

Before the first `add_subdirectory`, define:

```cmake
function(rss_enable_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
```

Add `add_subdirectory(libs/protocol)`. Remove protocol `.cpp` files from the temporary `rss_core` source list, link it with `rss_protocol`, and replace duplicated warning options with `rss_enable_project_warnings(rss_core)`. Keep `${PROJECT_SOURCE_DIR}/include` on the temporary `rss_core` target until Task 3 moves the remaining headers.

- [ ] **Step 4: 깨끗한 프로토콜 포함 경로를 검증한다**

Run:

```bash
cmake -S . -B build/refactor-protocol -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRSS_BUILD_NETWORK_TARGETS=OFF
cmake --build build/refactor-protocol --parallel
ctest --test-dir build/refactor-protocol --output-on-failure
```

Expected: 이전 플랫폼 독립 테스트가 모두 통과하고 `rss_core`가 `rss_protocol`에 연결된다.

- [ ] **Step 5: 프로토콜 분리를 커밋한다**

```bash
git add CMakeLists.txt libs/protocol include/rss/protocol src/protocol
git commit -m "refactor: extract protocol library"
```

---

### Task 3: 플랫폼 독립 서버 코어 분리

**Files:**
- Create: `libs/server-core/CMakeLists.txt`
- Move: `include/rss/domain/*` → `libs/server-core/include/rss/domain/`
- Move: `include/rss/service/*` → `libs/server-core/include/rss/service/`
- Move: `include/rss/util/*` → `libs/server-core/include/rss/util/`
- Move: portable `include/rss/net/{CompletionNotifier,OverloadStats,ReadBackpressureController,ServerConfig,Session,WorkerPool}.h` → `libs/server-core/include/rss/net/`
- Move: `src/domain/*` → `libs/server-core/src/domain/`
- Move: `src/service/*` → `libs/server-core/src/service/`
- Move: `src/net/{Session,WorkerPool}.cpp` → `libs/server-core/src/net/`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `rss_protocol`, `Threads::Threads`
- Produces: `rss_server_core` with public headers under `rss/{domain,service,net,util}`

- [ ] **Step 1: 플랫폼 독립 파일만 이동한다**

Do not move `EpollEventLoop`, `EventFdCompletionNotifier`, `TcpServer`, or `net/detail/AcceptBatchLimiter.h` in this task. Do not edit C++ content.

- [ ] **Step 2: 서버 코어 타깃을 정의한다**

Create `libs/server-core/CMakeLists.txt`:

```cmake
add_library(rss_server_core
    src/domain/Lobby.cpp
    src/domain/Room.cpp
    src/domain/User.cpp
    src/net/Session.cpp
    src/net/WorkerPool.cpp
    src/service/MessageRouter.cpp
    src/service/RoomService.cpp
)

target_include_directories(rss_server_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rss_server_core
    PUBLIC
        rss_protocol
        Threads::Threads
)

target_compile_features(rss_server_core PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_server_core)
```

- [ ] **Step 3: 임시 `rss_core`를 제거하고 소비자를 갱신한다**

Add `add_subdirectory(libs/server-core)` after `libs/protocol`. Remove the old `rss_core` declaration. Update the existing `rss_net` target and `rss_core_tests` target to link `rss_server_core`. Remove `src/net/Session.cpp` and `src/net/WorkerPool.cpp` from the test executable source list because the library now owns them. Update the optional inline `rss_microbenchmarks` target to link `rss_protocol` and `rss_server_core` instead of the removed `rss_core`, so benchmark-enabled configuration remains valid during the migration.

- [ ] **Step 4: 플랫폼 독립 build와 tests를 검증한다**

Run:

```bash
cmake -S . -B build/refactor-core -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRSS_BUILD_NETWORK_TARGETS=OFF
cmake --build build/refactor-core --parallel
ctest --test-dir build/refactor-core --output-on-failure
```

Expected: 모든 기존 플랫폼 독립 테스트가 통과한다.

- [ ] **Step 5: 서버 코어 분리를 커밋한다**

```bash
git add CMakeLists.txt libs/server-core include/rss/domain include/rss/service include/rss/util include/rss/net src/domain src/service src/net
git commit -m "refactor: extract portable server core"
```

---

### Task 4: Linux 네트워크와 애플리케이션 재배치

**Files:**
- Create: `libs/server-net-linux/CMakeLists.txt`
- Create: `libs/load-test-support/CMakeLists.txt`
- Create: `apps/server/CMakeLists.txt`
- Create: `apps/console-client/CMakeLists.txt`
- Create: `apps/load-test-client/CMakeLists.txt`
- Move: remaining Linux network headers and sources → `libs/server-net-linux`
- Move: `include/rss/tools/LatencyStats.h` → `libs/load-test-support/include/rss/tools/LatencyStats.h`
- Move: `src/main.cpp` → `apps/server/src/main.cpp`
- Move: `client/console_client.cpp` → `apps/console-client/src/main.cpp`
- Move: `tools/load_test_client.cpp` → `apps/load-test-client/src/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `rss_protocol`, `rss_server_core`, `rss_load_test_support`, `Threads::Threads`
- Produces: Linux-only `rss_server_net`, `rss_server`, `rss_console_client`, `rss_load_test_client`

- [ ] **Step 1: Linux 네트워크 파일과 애플리케이션 진입점을 이동한다**

Preserve all C++ content. Keep `rss/net/...` and `rss/tools/...` include spelling unchanged.

- [ ] **Step 2: Linux 네트워크 타깃을 정의한다**

Create `libs/server-net-linux/CMakeLists.txt`:

```cmake
add_library(rss_server_net
    src/EpollEventLoop.cpp
    src/EventFdCompletionNotifier.cpp
    src/TcpServer.cpp
)

target_include_directories(rss_server_net
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rss_server_net
    PUBLIC
        rss_server_core
        rss_protocol
        Threads::Threads
)

target_compile_features(rss_server_net PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_server_net)
```

- [ ] **Step 3: 지원 라이브러리와 세 애플리케이션 타깃을 정의한다**

Create `libs/load-test-support/CMakeLists.txt`:

```cmake
add_library(rss_load_test_support INTERFACE)
target_include_directories(rss_load_test_support
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(rss_load_test_support INTERFACE cxx_std_20)
```

Create app CMake files:

```cmake
# apps/server/CMakeLists.txt
add_executable(rss_server src/main.cpp)
target_link_libraries(rss_server PRIVATE rss_server_net)
rss_enable_project_warnings(rss_server)

# apps/console-client/CMakeLists.txt
add_executable(rss_console_client src/main.cpp)
target_link_libraries(rss_console_client PRIVATE rss_protocol Threads::Threads)
rss_enable_project_warnings(rss_console_client)

# apps/load-test-client/CMakeLists.txt
add_executable(rss_load_test_client src/main.cpp)
target_link_libraries(rss_load_test_client
    PRIVATE
        rss_protocol
        rss_load_test_support
        Threads::Threads
)
rss_enable_project_warnings(rss_load_test_client)
```

- [ ] **Step 4: 루트의 Linux 조건부 조합을 단순화한다**

Replace inline Linux target declarations with:

```cmake
add_subdirectory(libs/load-test-support)

if(RSS_BUILD_NETWORK_TARGETS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(libs/server-net-linux)
    add_subdirectory(apps/server)
    add_subdirectory(apps/console-client)
    add_subdirectory(apps/load-test-client)
else()
    message(STATUS "Linux-only epoll server/client targets are disabled on this platform.")
endif()
```

While the tests and benchmarks are still declared inline, add `rss_load_test_support` to `rss_core_tests` and `rss_microbenchmarks`. This preserves the moved `rss/tools/LatencyStats.h` include until Tasks 5 and 6 split those consumers into their final directories.

- [ ] **Step 5: 비 Linux configure가 Linux 헤더를 읽지 않는지 검증한다**

Run:

```bash
cmake -S . -B build/refactor-apps -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRSS_BUILD_NETWORK_TARGETS=OFF
cmake --build build/refactor-apps --parallel
ctest --test-dir build/refactor-apps --output-on-failure
```

Expected: macOS에서 Linux 헤더 오류 없이 모든 플랫폼 독립 테스트가 통과한다.

- [ ] **Step 6: Linux 네트워크와 애플리케이션 이동을 커밋한다**

```bash
git add CMakeLists.txt apps libs/server-net-linux libs/load-test-support client include/rss/net include/rss/tools src/main.cpp src/net tools
git commit -m "refactor: organize Linux apps and networking"
```

---

### Task 5: 테스트를 라이브러리별로 분리

**Files:**
- Create: `tests/CMakeLists.txt`
- Create: `tests/protocol/CMakeLists.txt`
- Create: `tests/server-core/CMakeLists.txt`
- Create: `tests/load-test-support/CMakeLists.txt`
- Create: `tests/server-net-linux/CMakeLists.txt`
- Move: all files under `test/` to the matching `tests/*` directory
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `rss_protocol`, `rss_server_core`, `rss_server_net`, `rss_load_test_support`, `GTest::gtest_main`
- Produces: `rss_protocol_tests`, `rss_server_core_tests`, `rss_load_test_support_tests`, `rss_server_net_tests`

- [ ] **Step 1: 테스트 파일을 책임별로 이동한다**

Move exactly:

```text
tests/protocol:
  PacketCodecTest.cpp

tests/load-test-support:
  LatencyStatsTest.cpp

tests/server-net-linux:
  EpollEventLoopTest.cpp
  EventFdCompletionNotifierTest.cpp
  TcpServerBackpressureTest.cpp

tests/server-core:
  BoundedBlockingQueueTest.cpp
  MessageRouterTest.cpp
  OverloadStatsTest.cpp
  ReadBackpressureControllerTest.cpp
  RoomServiceTest.cpp
  ServerConfigTest.cpp
  SessionTest.cpp
  WorkerPoolNotificationTest.cpp
```

- [ ] **Step 2: 테스트 하위 프로젝트 조합을 정의한다**

Create `tests/CMakeLists.txt`:

```cmake
add_subdirectory(protocol)
add_subdirectory(server-core)
add_subdirectory(load-test-support)

if(RSS_BUILD_NETWORK_TARGETS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(server-net-linux)
endif()
```

- [ ] **Step 3: 각 테스트 실행 파일을 정의한다**

Create `tests/protocol/CMakeLists.txt`:

```cmake
add_executable(rss_protocol_tests PacketCodecTest.cpp)
target_link_libraries(rss_protocol_tests PRIVATE rss_protocol GTest::gtest_main)
gtest_discover_tests(rss_protocol_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 15)
```

Create `tests/server-core/CMakeLists.txt`:

```cmake
add_executable(rss_server_core_tests
    BoundedBlockingQueueTest.cpp
    MessageRouterTest.cpp
    OverloadStatsTest.cpp
    ReadBackpressureControllerTest.cpp
    RoomServiceTest.cpp
    ServerConfigTest.cpp
    SessionTest.cpp
    WorkerPoolNotificationTest.cpp
)
target_link_libraries(rss_server_core_tests
    PRIVATE
        rss_server_core
        GTest::gtest_main
)
gtest_discover_tests(
    rss_server_core_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 15
)
```

Create `tests/load-test-support/CMakeLists.txt`:

```cmake
add_executable(rss_load_test_support_tests LatencyStatsTest.cpp)
target_link_libraries(rss_load_test_support_tests
    PRIVATE
        rss_load_test_support
        GTest::gtest_main
)
gtest_discover_tests(
    rss_load_test_support_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 15
)
```

Create `tests/server-net-linux/CMakeLists.txt`:

```cmake
add_executable(rss_server_net_tests
    EpollEventLoopTest.cpp
    EventFdCompletionNotifierTest.cpp
    TcpServerBackpressureTest.cpp
)
target_link_libraries(rss_server_net_tests
    PRIVATE
        rss_server_net
        GTest::gtest_main
)
gtest_discover_tests(
    rss_server_net_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 30
)
```

- [ ] **Step 4: 루트 테스트 블록을 하위 디렉터리 조합으로 교체한다**

Keep `enable_testing()`, `rss_enable_googletest()`, and `include(GoogleTest)`, then replace inline test executable declarations with `add_subdirectory(tests)`.

- [ ] **Step 5: 테스트 이름과 개수를 기준선과 비교한다**

Run:

```bash
cmake -S . -B build/refactor-tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRSS_BUILD_NETWORK_TARGETS=OFF
cmake --build build/refactor-tests --parallel
ctest --test-dir build/refactor-tests -N > /tmp/rss-tests-after.txt
ctest --test-dir build/refactor-tests --output-on-failure
```

Expected: 모든 테스트가 통과하고 총 플랫폼 독립 test case 수가 Task 1 기준선과 같다. Test suite와 case 이름이 바뀌지 않는다.

- [ ] **Step 6: 테스트 분리를 커밋한다**

```bash
git add CMakeLists.txt tests test
git commit -m "test: organize suites by library"
```

---

### Task 6: 벤치마크, 코드 품질 경로, CMake preset 정리

**Files:**
- Create: `benchmarks/CMakeLists.txt`
- Create: `CMakePresets.json`
- Move: `benchmark/*.cpp` → `benchmarks/*.cpp`
- Modify: `CMakeLists.txt`
- Modify: `cmake/CodeQuality.cmake`

**Interfaces:**
- Consumes: `rss_protocol`, `rss_server_core`, `rss_load_test_support`, `benchmark::benchmark_main`
- Produces: `rss_microbenchmarks`, 공통 개발 및 CI용 CMake preset

- [ ] **Step 1: 벤치마크 소스를 이동하고 타깃을 정의한다**

Create `benchmarks/CMakeLists.txt`:

```cmake
add_executable(rss_microbenchmarks
    LatencyStatsBenchmark.cpp
    MessageRouterBenchmark.cpp
    PacketCodecBenchmark.cpp
)

target_link_libraries(rss_microbenchmarks
    PRIVATE
        rss_protocol
        rss_server_core
        rss_load_test_support
        benchmark::benchmark_main
)

rss_enable_project_warnings(rss_microbenchmarks)
```

In the root benchmark block, keep `rss_enable_google_benchmark()` and replace the inline executable with `add_subdirectory(benchmarks)`.

- [ ] **Step 2: 코드 품질 탐색 경로를 새 구조로 바꾼다**

Update `cmake/CodeQuality.cmake` so `GLOB_RECURSE` searches only:

```cmake
"${PROJECT_SOURCE_DIR}/apps/*.cpp"
"${PROJECT_SOURCE_DIR}/apps/*.h"
"${PROJECT_SOURCE_DIR}/benchmarks/*.cpp"
"${PROJECT_SOURCE_DIR}/libs/*.cpp"
"${PROJECT_SOURCE_DIR}/libs/*.h"
"${PROJECT_SOURCE_DIR}/tests/*.cpp"
"${PROJECT_SOURCE_DIR}/tests/*.h"
```

Update both clang-tidy regular expressions from the old directory set to `^(apps|benchmarks|libs|tests)/`.

- [ ] **Step 3: CMake 3.20 호환 preset을 추가한다**

Create `CMakePresets.json` with schema version 2 and these configure presets:

```json
{
  "version": 2,
  "configurePresets": [
    {
      "name": "core-dev",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/core-dev",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "RSS_BUILD_NETWORK_TARGETS": "OFF"
      }
    },
    {
      "name": "linux-dev",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/linux-dev",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "RSS_BUILD_NETWORK_TARGETS": "ON"
      }
    },
    {
      "name": "release",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release"
      }
    },
    {
      "name": "benchmark",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/benchmark",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "RSS_BUILD_NETWORK_TARGETS": "OFF",
        "RSS_BUILD_BENCHMARKS": "ON"
      }
    }
  ],
  "buildPresets": [
    { "name": "core-dev", "configurePreset": "core-dev" },
    { "name": "linux-dev", "configurePreset": "linux-dev" },
    { "name": "release", "configurePreset": "release" },
    { "name": "benchmark", "configurePreset": "benchmark" }
  ],
  "testPresets": [
    {
      "name": "core-dev",
      "configurePreset": "core-dev",
      "output": { "outputOnFailure": true }
    },
    {
      "name": "linux-dev",
      "configurePreset": "linux-dev",
      "output": { "outputOnFailure": true }
    }
  ]
}
```

- [ ] **Step 4: preset과 benchmark를 검증한다**

Run:

```bash
cmake --list-presets
cmake --preset core-dev
cmake --build --preset core-dev
ctest --preset core-dev
cmake --preset benchmark
cmake --build --preset benchmark --target rss_microbenchmarks
./build/benchmark/rss_microbenchmarks --benchmark_dry_run
```

Expected: preset이 모두 표시되고 core tests와 benchmark dry-run이 성공한다.

- [ ] **Step 5: 벤치마크와 preset 변경을 커밋한다**

```bash
git add CMakeLists.txt CMakePresets.json benchmarks benchmark cmake/CodeQuality.cmake
git commit -m "build: add monorepo presets and benchmark target"
```

---

### Task 7: 문서와 CI를 새 구조에 맞춘다

**Files:**
- Modify: `README.md`
- Modify: `CONTRIBUTING.md`
- Modify: `docs/architecture.md`
- Modify: `docs/benchmark.md`
- Modify if paths occur: `docs/development-plan.md`, `docs/protocol.md`
- Modify: `.github/workflows/ci.yml`
- Verify: `.vscode/extensions.json`

**Interfaces:**
- Consumes: Task 2~6의 최종 경로와 preset 이름
- Produces: 공개 저장소 사용자가 그대로 실행할 수 있는 한글 빌드 및 구조 문서와 세 운영체제 CI

- [ ] **Step 1: 이전 경로 참조를 전부 찾는다**

Run:

```bash
rg -n '(^|`|\(|/)(include/rss|src/|client/|tools/|test/|benchmark/)' \
  README.md CONTRIBUTING.md docs/*.md .github .vscode
```

Record every match and classify it as a filesystem path or a generic term before editing.

- [ ] **Step 2: 구조와 빌드 문서를 갱신한다**

Update directory trees and source paths to `apps/`, `libs/`, `tests/`, and `benchmarks/`. Replace repeated manual development commands with the matching preset commands while retaining explicit manual CMake commands where they teach configuration options. Do not change protocol semantics or server behavior descriptions.

- [ ] **Step 3: CI가 새 타깃과 세 플랫폼 경계를 검증하도록 갱신한다**

Keep the existing macOS, Windows, Ubuntu, sanitizer, and code-quality jobs. Ensure:

```text
macOS/Windows: RSS_BUILD_NETWORK_TARGETS=OFF
Ubuntu build-and-test: RSS_BUILD_NETWORK_TARGETS=ON and RSS_BUILD_BENCHMARKS=ON
Sanitizers: complete Linux targets
Code quality: new apps/benchmarks/libs/tests paths
```

Keep `ctest --output-on-failure` and `rss_microbenchmarks --benchmark_dry_run` in CI. Do not add Qt installation steps.

- [ ] **Step 4: 오래된 경로와 공개 문서 규칙을 검사한다**

Run:

```bash
rg -n 'include/rss|src/|client/|tools/|test/|benchmark/' \
  README.md CONTRIBUTING.md docs/*.md .github .vscode
rg -n -i 'password|secret|api[_-]?key|access[_-]?token' \
  AGENTS.md README.md CONTRIBUTING.md docs
git diff --check
```

Expected: 남은 이전 경로는 역사 설명이나 명령상 의도된 경우뿐이며 직접 검토된다. 공개 문서에 인증정보나 불필요한 개인적 배경이 없다.

- [ ] **Step 5: 문서와 CI 변경을 커밋한다**

```bash
git add README.md CONTRIBUTING.md docs .github/workflows/ci.yml .vscode/extensions.json
git commit -m "docs: update monorepo build and layout guidance"
```

---

### Task 8: 전체 회귀 검증과 PR 준비

**Files:**
- Verify: entire repository
- Modify only if verification exposes an in-scope path, CMake, documentation, or configuration defect

**Interfaces:**
- Consumes: 완성된 모노레포 구조
- Produces: PR 생성 가능한 깨끗한 브랜치와 검증 증거

- [ ] **Step 1: 파일 이동 누락을 검사한다**

Run:

```bash
find apps libs tests benchmarks -type f | sort
find include src client tools test benchmark -type f 2>/dev/null
git status --short
git diff main...HEAD --check
```

Expected: 첫 명령에는 계획된 모든 파일이 있고, 두 번째 명령은 이전 소스 경로에 파일을 출력하지 않는다. 작업 트리는 의도한 변경만 포함한다.

- [ ] **Step 2: 깨끗한 플랫폼 독립 Debug 검증을 실행한다**

Run:

```bash
cmake -S . -B build/verify-core -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRSS_BUILD_NETWORK_TARGETS=OFF
cmake --build build/verify-core --parallel
ctest --test-dir build/verify-core --output-on-failure
```

Expected: configure/build 성공, 실패 테스트 0개, Task 1과 동일한 플랫폼 독립 test case 수.

- [ ] **Step 3: 깨끗한 Release benchmark 검증을 실행한다**

Run:

```bash
cmake -S . -B build/verify-benchmark -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DRSS_BUILD_NETWORK_TARGETS=OFF \
  -DRSS_BUILD_BENCHMARKS=ON
cmake --build build/verify-benchmark --target rss_microbenchmarks --parallel
./build/verify-benchmark/rss_microbenchmarks --benchmark_dry_run
```

Expected: 모든 benchmark가 dry-run으로 실행되고 실패가 없다.

- [ ] **Step 4: 코드 품질 검사를 실행한다**

Run:

```bash
cmake --build --preset core-dev --target format-check
cmake --build --preset core-dev --target tidy-check
```

Expected: 설치된 도구에 해당하는 타깃은 성공한다. `tidy-check`가 이 장비에 없으면 CMake configure 메시지와 Linux CI 검증 필요성을 PR에 명시한다.

- [ ] **Step 5: 커밋과 범위를 최종 검토한다**

Run:

```bash
git log --oneline main..HEAD
git diff --stat main...HEAD
git diff main...HEAD -- '*.cpp' '*.h'
git status --short --branch
```

Expected: C++ diff는 이동으로 인식되거나 include/build 소유권에 필요한 변경만 포함한다. Qt 코드와 런타임 동작 변경이 없고 작업 트리가 깨끗하다.

- [ ] **Step 6: 브랜치 완료 절차로 전환한다**

전체 변경을 코드 리뷰한 뒤 `codex/refactor-monorepo-layout` 브랜치를 push하고 `main` 대상 draft PR을 생성한다. GitHub Actions 결과를 확인하고, 필수 검사가 모두 통과한 뒤 사용자 승인에 따라 병합한다. `main`에 직접 커밋하거나 사용자 승인 없이 병합하지 않는다.
