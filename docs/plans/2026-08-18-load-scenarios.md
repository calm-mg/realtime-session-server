# 실제 서버 부하 시나리오 구현 계획

> 구현 단계와 검증 명령은 체크박스(`- [ ]`) 단위로 기록한다.

**Goal:** 실제 `TcpServer`와 TCP 클라이언트를 한 프로세스에서 구동해 broadcast, multi-room, slow-client 시나리오를 반복 측정하는 Linux 전용 실행기를 추가한다.

**Architecture:** 플랫폼 독립적인 인자·결과 계산은 `rss_load_test_support`에 두고, Linux socket과 `TcpServer` 수명 주기는 새 `rss_load_scenario_runtime`에 둔다. `rss_load_scenario_runner`는 두 라이브러리를 조립하며, 기존 원격 PING 도구는 변경하지 않는다.

**Tech Stack:** C++20, CMake, POSIX TCP sockets, Linux `epoll`, GoogleTest, 기존 `rss_protocol`, `rss_server_core`, `rss_server_net`

**Spec:** `docs/design/2026-08-18-load-scenarios.md`

## Global Constraints

- 서버 네트워크 코드와 시나리오 실행기는 Linux에서만 빌드한다.
- `libs/load-test-support`는 `server-core`와 `server-net-linux`에 의존하지 않는다.
- socket과 `epoll` 상태는 기존 규칙대로 I/O 스레드만 변경한다.
- 모든 socket 대기와 서버 시작 대기는 유한한 timeout을 사용한다.
- 공개 include는 `rss/...` 형식을 유지한다.
- 기존 `rss_load_test_client`의 인자와 출력은 변경하지 않는다.
- 성능 테스트는 처리량 절대값을 assert하지 않고 정확성과 종료만 검증한다.
- 문서와 커밋 메시지는 한글을 기본으로 한다.

---

### Task 1: 플랫폼 독립 시나리오 옵션과 결과 모델

**Files:**
- Modify: `libs/load-test-support/CMakeLists.txt`
- Create: `libs/load-test-support/include/rss/tools/ScenarioOptions.h`
- Create: `libs/load-test-support/include/rss/tools/ScenarioReport.h`
- Create: `libs/load-test-support/src/ScenarioOptions.cpp`
- Create: `libs/load-test-support/src/ScenarioReport.cpp`
- Modify: `tests/load-test-support/CMakeLists.txt`
- Create: `tests/load-test-support/ScenarioOptionsTest.cpp`
- Create: `tests/load-test-support/ScenarioReportTest.cpp`

**Interfaces:**
- Produces: `rss::tools::ScenarioKind`, `ScenarioOptions`, `parseScenarioOptions()`, `scenarioName()`
- Produces: `OverloadReport`, `ScenarioRunResult`, `expectedBroadcasts()`, `isSuccessful()`, `formatRunResult()`
- Consumes: 기존 `LatencyStats.h`의 `LatencyReport`와 `latencyReport()`

- [ ] **Step 1: 옵션 파싱 실패 테스트 작성**

```cpp
TEST(ScenarioOptionsTest, ParsesBroadcastArguments) {
  const std::array<std::string_view, 12> args{
      "--scenario", "broadcast", "--clients", "20", "--messages", "50",
      "--payload-bytes", "512", "--repeat", "3", "--workers", "2"};
  const auto options = rss::tools::parseScenarioOptions(args);
  EXPECT_EQ(options.scenario, rss::tools::ScenarioKind::Broadcast);
  EXPECT_EQ(options.clients, 20U);
  EXPECT_EQ(options.messages_per_sender, 50U);
  EXPECT_EQ(options.payload_bytes, 512U);
  EXPECT_EQ(options.repeats, 3U);
  EXPECT_EQ(options.worker_count, 2U);
}

TEST(ScenarioOptionsTest, RejectsRoomCountAboveClientCount) {
  const std::array<std::string_view, 6> args{
      "--scenario", "multi-room", "--clients", "2", "--rooms", "3"};
  EXPECT_THROW(rss::tools::parseScenarioOptions(args), std::invalid_argument);
}

TEST(ScenarioOptionsTest, RejectsSlowClientCountAtClientCount) {
  const std::array<std::string_view, 6> args{
      "--scenario", "slow-client", "--clients", "2", "--slow-clients", "2"};
  EXPECT_THROW(rss::tools::parseScenarioOptions(args), std::invalid_argument);
}
```

- [ ] **Step 2: 옵션 테스트가 컴파일 실패하는지 확인**

Run: `cmake --preset core-dev && cmake --build --preset core-dev --target rss_load_test_support_tests`

Expected: `rss/tools/ScenarioOptions.h`가 없어 compile 실패.

- [ ] **Step 3: 옵션 계약과 파서 최소 구현**

```cpp
enum class ScenarioKind { Broadcast, MultiRoom, SlowClient };

struct ScenarioOptions {
  ScenarioKind scenario{ScenarioKind::Broadcast};
  std::size_t clients{10};
  std::size_t rooms{2};
  std::size_t messages_per_sender{100};
  std::size_t payload_bytes{256};
  std::size_t slow_clients{1};
  std::size_t repeats{5};
  std::size_t worker_count{4};
};

ScenarioOptions parseScenarioOptions(std::span<const std::string_view> args);
std::string_view scenarioName(ScenarioKind kind) noexcept;
```

파서는 알려지지 않은 옵션, 값이 빠진 옵션, 숫자가 아닌 값, 0, 64 미만 또는
4000 초과 payload, 잘못된 rooms/slow_clients 관계를 `std::invalid_argument`로
거부한다.

- [ ] **Step 4: 옵션 테스트 통과 확인**

Run: `cmake --build --preset core-dev --target rss_load_test_support_tests && ctest --preset core-dev -R ScenarioOptionsTest`

Expected: 모든 `ScenarioOptionsTest` PASS.

- [ ] **Step 5: 결과 계산 실패 테스트 작성**

```cpp
TEST(ScenarioReportTest, CalculatesExpectedBroadcastsAcrossRooms) {
  const std::array<std::size_t, 2> room_sizes{3, 2};
  const std::array<std::uint64_t, 2> successful_sends{12, 8};
  EXPECT_EQ(rss::tools::expectedBroadcasts(room_sizes, successful_sends), 52U);
}

TEST(ScenarioReportTest, FailsWhenAnyObservableErrorExists) {
  rss::tools::ScenarioRunResult result;
  result.expected_broadcasts = 10;
  result.received_broadcasts = 9;
  result.missing_broadcasts = 1;
  EXPECT_FALSE(rss::tools::isSuccessful(
      rss::tools::ScenarioKind::Broadcast, result, 0));
}

TEST(ScenarioReportTest, RequiresConfiguredSlowClientDisconnects) {
  rss::tools::ScenarioRunResult result;
  result.overload.slow_client_disconnects = 1;
  EXPECT_FALSE(rss::tools::isSuccessful(
      rss::tools::ScenarioKind::SlowClient, result, 2));
  result.overload.slow_client_disconnects = 2;
  EXPECT_TRUE(rss::tools::isSuccessful(
      rss::tools::ScenarioKind::SlowClient, result, 2));
}
```

- [ ] **Step 6: 결과 테스트가 컴파일 실패하는지 확인**

Run: `cmake --build --preset core-dev --target rss_load_test_support_tests`

Expected: `ScenarioReport` 타입과 함수가 없어 compile 실패.

- [ ] **Step 7: 결과 모델과 계산 최소 구현**

```cpp
struct OverloadReport {
  std::uint64_t read_pauses{};
  std::uint64_t read_resumes{};
  std::uint64_t inbound_queue_full{};
  std::uint64_t outbound_budget_rejections{};
  std::uint64_t slow_client_disconnects{};
  std::uint64_t rejected_connections{};
  std::size_t max_inbound_queue_size{};
  std::size_t max_outbound_queue_size{};
  std::size_t max_session_pending_write_bytes{};
};

struct ScenarioRunResult {
  ScenarioOptions requested;
  std::size_t effective_rooms{1};
  std::uint64_t sent{};
  std::uint64_t expected_broadcasts{};
  std::uint64_t received_broadcasts{};
  std::uint64_t missing_broadcasts{};
  std::uint64_t duplicate_broadcasts{};
  std::uint64_t unexpected_broadcasts{};
  std::uint64_t failed_clients{};
  std::vector<std::chrono::microseconds> latencies;
  OverloadReport overload;
  std::chrono::microseconds elapsed{};
};

std::uint64_t expectedBroadcasts(std::span<const std::size_t> room_sizes,
                                 std::span<const std::uint64_t>
                                     successful_sends_by_room);
bool isSuccessful(ScenarioKind kind, const ScenarioRunResult& result,
                  std::size_t required_slow_disconnects) noexcept;
std::string formatRunResult(std::size_t run, ScenarioKind kind,
                            const ScenarioRunResult& result);
```

`formatRunResult()`는 설계 문서의 필드 순서를 그대로 사용하고 기존
`latencyReport()`로 p50/p95/p99를 계산한다.

- [ ] **Step 8: 지원 코드 전체 테스트 통과 확인**

Run: `cmake --build --preset core-dev --target rss_load_test_support_tests && ctest --preset core-dev -R 'Scenario(Options|Report)Test'`

Expected: 새 테스트 모두 PASS.

- [ ] **Step 9: 커밋**

```bash
git add libs/load-test-support tests/load-test-support
git commit -m "기능: 부하 시나리오 옵션과 결과 모델 추가"
```

### Task 2: 내장 실제 서버의 안전한 수명 주기

**Files:**
- Modify: `CMakeLists.txt`
- Create: `apps/load-scenario-runner/CMakeLists.txt`
- Create: `apps/load-scenario-runner/src/EmbeddedServer.h`
- Create: `apps/load-scenario-runner/src/EmbeddedServer.cpp`
- Modify: `tests/server-net-linux/CMakeLists.txt`
- Create: `tests/server-net-linux/EmbeddedServerTest.cpp`

**Interfaces:**
- Consumes: `rss::net::ServerConfig`, `TcpServer::run()`, `stop()`, `boundPort()`, `overloadSnapshot()`
- Produces: `rss::tools::EmbeddedServer::start()`, `stop()`, `port()`, `snapshot()`
- Produces: CMake target `rss_load_scenario_runtime`

- [ ] **Step 1: 서버 시작·종료 실패 테스트 작성**

```cpp
TEST(EmbeddedServerTest, StartsOnAnEphemeralLoopbackPortAndStops) {
  rss::net::ServerConfig config;
  config.host = "127.0.0.1";
  config.port = 0;
  config.worker_count = 1;

  rss::tools::EmbeddedServer server(config);
  server.start(2s);
  EXPECT_NE(server.port(), 0);
  EXPECT_EQ(server.snapshot().current_sessions, 0U);
  server.stop();
}

TEST(EmbeddedServerTest, RejectsStartingTwice) {
  rss::net::ServerConfig config;
  config.host = "127.0.0.1";
  config.port = 0;
  rss::tools::EmbeddedServer server(config);
  server.start(2s);
  EXPECT_THROW(server.start(2s), std::logic_error);
}
```

- [ ] **Step 2: Linux 테스트 compile 실패 확인**

Run: `cmake --preset linux-dev && cmake --build --preset linux-dev --target rss_server_net_tests`

Expected: `EmbeddedServer.h`가 없어 compile 실패.

- [ ] **Step 3: 런타임 target과 RAII 서버 최소 구현**

```cpp
class EmbeddedServer {
 public:
  explicit EmbeddedServer(rss::net::ServerConfig config);
  ~EmbeddedServer();
  EmbeddedServer(const EmbeddedServer&) = delete;
  EmbeddedServer& operator=(const EmbeddedServer&) = delete;

  void start(std::chrono::milliseconds timeout);
  void stop();
  std::uint16_t port() const noexcept;
  rss::net::OverloadSnapshot snapshot() const;

 private:
  rss::net::TcpServer server_;
  std::thread thread_;
  std::exception_ptr failure_;
  std::mutex failure_mutex_;
  bool started_{};
};
```

`start()`는 서버 스레드를 시작한 뒤 `boundPort() != 0`, 저장된 예외, timeout
중 하나가 발생할 때까지 1 ms 간격으로 기다린다. 명시적으로 호출한 `stop()`은
`TcpServer::stop()` 후 join하고 서버 스레드가 저장한 예외를 다시 던진다.
destructor는 같은 정리 절차를 수행하되 예외를 삼킨다.

- [ ] **Step 4: EmbeddedServer 테스트 통과 확인**

Run: `cmake --build --preset linux-dev --target rss_server_net_tests && ctest --preset linux-dev -R EmbeddedServerTest`

Expected: 2 tests PASS, process hang 없음.

- [ ] **Step 5: 커밋**

```bash
git add CMakeLists.txt apps/load-scenario-runner tests/server-net-linux
git commit -m "기능: 부하 측정용 내장 서버 수명 주기 추가"
```

### Task 3: 실제 TCP 시나리오 클라이언트

**Files:**
- Modify: `apps/load-scenario-runner/CMakeLists.txt`
- Create: `apps/load-scenario-runner/src/ScenarioClient.h`
- Create: `apps/load-scenario-runner/src/ScenarioClient.cpp`
- Create: `tests/server-net-linux/ScenarioClientTest.cpp`
- Modify: `tests/server-net-linux/CMakeLists.txt`

**Interfaces:**
- Consumes: `rss::protocol::PacketCodec`, `PacketType`, `EmbeddedServer`
- Produces: `ScenarioClient::connect()`, `login()`, `createRoom()`, `joinRoom()`, `sendChat()`, `receivePacket()`, `setReceiveBufferBytes()`

- [ ] **Step 1: 실서버 왕복 실패 테스트 작성**

```cpp
TEST(ScenarioClientTest, LogsInCreatesRoomAndReceivesOwnChat) {
  auto server = startTestServer();
  rss::tools::ScenarioClient client;
  client.connect("127.0.0.1", server.port(), 2s);
  client.login("alice", 2s);
  const auto room_id = client.createRoom("room", 2s);
  ASSERT_NE(room_id, 0U);

  client.sendChat("run=1;sender=0;seq=0;sent_us=1", 2s);
  const auto packet = client.receivePacket(2s);
  EXPECT_EQ(packet.type, rss::protocol::PacketType::RoomBroadcast);
  EXPECT_NE(rss::protocol::payloadToString(packet).find("event=CHAT"),
            std::string::npos);
}
```

- [ ] **Step 2: ScenarioClient test compile 실패 확인**

Run: `cmake --build --preset linux-dev --target rss_server_net_tests`

Expected: `ScenarioClient.h`가 없어 compile 실패.

- [ ] **Step 3: socket 소유권과 timeout 기반 I/O 구현**

```cpp
class ScenarioClient {
 public:
  ScenarioClient() = default;
  ~ScenarioClient();
  ScenarioClient(ScenarioClient&& other) noexcept;
  ScenarioClient& operator=(ScenarioClient&& other) noexcept;
  ScenarioClient(const ScenarioClient&) = delete;

  void connect(std::string_view host, std::uint16_t port,
               std::chrono::milliseconds timeout);
  void setReceiveBufferBytes(int bytes);
  void login(std::string_view name, std::chrono::milliseconds timeout);
  std::uint32_t createRoom(std::string_view name,
                           std::chrono::milliseconds timeout);
  void joinRoom(std::uint32_t room_id, std::chrono::milliseconds timeout);
  void sendChat(std::string_view payload,
                std::chrono::milliseconds timeout);
  rss::protocol::Packet receivePacket(std::chrono::milliseconds timeout);
  void close() noexcept;

 private:
  void sendPacket(rss::protocol::PacketType type, std::string_view payload,
                  std::chrono::milliseconds timeout);
  rss::protocol::Packet waitFor(rss::protocol::PacketType expected,
                                std::chrono::milliseconds timeout);
  int fd_{-1};
  rss::protocol::PacketCodec codec_;
};
```

`poll()`로 connect/send/receive timeout을 구현하고 `EINTR`은 남은 deadline로
재시도한다. 응답 대기 중 `ERROR` 패킷을 받으면 payload를 포함한
`std::runtime_error`를 던진다. `createRoom()`은 `room_id=` 다음의 10진수를
`std::from_chars`로 파싱한다.

- [ ] **Step 4: 분할·다중 패킷 수신 테스트 추가**

실제 서버에 두 클라이언트를 연결해 join 알림과 chat broadcast가 같은
수신 버퍼에 함께 도착해도 `receivePacket()`이 순서대로 반환하는 테스트를
추가한다.

- [ ] **Step 5: ScenarioClient 테스트 통과 확인**

Run: `cmake --build --preset linux-dev --target rss_server_net_tests && ctest --preset linux-dev -R ScenarioClientTest`

Expected: 모든 `ScenarioClientTest` PASS.

- [ ] **Step 6: 커밋**

```bash
git add apps/load-scenario-runner tests/server-net-linux
git commit -m "기능: 부하 시나리오 TCP 클라이언트 추가"
```

### Task 4: 단일 방 broadcast 시나리오

**Files:**
- Modify: `apps/load-scenario-runner/CMakeLists.txt`
- Create: `apps/load-scenario-runner/src/ScenarioRunner.h`
- Create: `apps/load-scenario-runner/src/ScenarioRunner.cpp`
- Create: `tests/server-net-linux/ScenarioRunnerTest.cpp`
- Modify: `tests/server-net-linux/CMakeLists.txt`

**Interfaces:**
- Consumes: `ScenarioOptions`, `ScenarioRunResult`, `EmbeddedServer`, `ScenarioClient`
- Produces: `ScenarioRunner::runOnce(const ScenarioOptions&, std::size_t run_id)`

- [ ] **Step 1: 작은 broadcast 실패 테스트 작성**

```cpp
TEST(ScenarioRunnerTest, BroadcastDeliversEveryMessageToEveryClient) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::Broadcast;
  options.clients = 2;
  options.messages_per_sender = 3;
  options.payload_bytes = 128;
  options.worker_count = 2;

  const auto result = rss::tools::ScenarioRunner{}.runOnce(options, 1);
  EXPECT_EQ(result.sent, 6U);
  EXPECT_EQ(result.expected_broadcasts, 12U);
  EXPECT_EQ(result.received_broadcasts, 12U);
  EXPECT_EQ(result.missing_broadcasts, 0U);
  EXPECT_EQ(result.duplicate_broadcasts, 0U);
  EXPECT_EQ(result.unexpected_broadcasts, 0U);
  EXPECT_EQ(result.failed_clients, 0U);
  EXPECT_EQ(result.latencies.size(), 12U);
}
```

- [ ] **Step 2: ScenarioRunner test compile 실패 확인**

Run: `cmake --build --preset linux-dev --target rss_server_net_tests`

Expected: `ScenarioRunner.h`가 없어 compile 실패.

- [ ] **Step 3: 메시지 식별자 helper와 실패 테스트 작성**

```cpp
TEST(ScenarioRunnerTest, MessageIdentityRoundTripsAtRequestedPayloadSize) {
  const auto payload = rss::tools::makeScenarioPayload(2, 3, 4, 123456, 128);
  EXPECT_EQ(payload.size(), 128U);
  const auto identity = rss::tools::parseScenarioPayload(payload);
  EXPECT_EQ(identity.run, 2U);
  EXPECT_EQ(identity.sender, 3U);
  EXPECT_EQ(identity.sequence, 4U);
  EXPECT_EQ(identity.sent_us, 123456U);
}
```

- [ ] **Step 4: 식별자와 broadcast 실행 최소 구현**

```cpp
struct MessageIdentity {
  std::size_t run{};
  std::size_t sender{};
  std::size_t sequence{};
  std::uint64_t sent_us{};
};

std::string makeScenarioPayload(std::size_t run, std::size_t sender,
                                std::size_t sequence, std::uint64_t sent_us,
                                std::size_t payload_bytes);
MessageIdentity parseScenarioPayload(std::string_view payload);

class ScenarioRunner {
 public:
  ScenarioRunResult runOnce(const ScenarioOptions& options,
                            std::size_t run_id) const;
};
```

`runOnce()`는 서버와 모든 클라이언트를 준비한 뒤 `std::barrier`로 receiver와
sender 시작을 맞춘다. 수신 작업은 `event=CHAT`만 집계하고 JOIN 알림은
버린다. 각 클라이언트가 보유한 `unordered_set`으로 중복을 판정하며 thread
join 이후 결과를 합산한다. 마지막 barrier 참여자가 도착한 completion에서
측정 시작 시각과 deadline을 설정해 준비 scheduling 시간을 제외한다. 송신
작업은 방별 성공 전송 수와 완료 상태를 게시하며, receiver는 최종 성공 수만
기대한다.

- [ ] **Step 5: broadcast 통합 테스트 통과 확인**

Run: `cmake --build --preset linux-dev --target rss_server_net_tests && ctest --preset linux-dev -R 'ScenarioRunnerTest.*(Broadcast|MessageIdentity)'`

Expected: broadcast와 payload tests PASS.

- [ ] **Step 6: timeout 실패 테스트 추가**

측정 deadline을 테스트 전용 생성자 인자로 1 ms로 설정하고 기대 수신을
완료하지 못하면 `missing_broadcasts > 0`이며 process는 종료되는 테스트를
추가한다. 절대 처리 시간을 assert하지 않는다.

- [ ] **Step 7: 전체 ScenarioRunner 테스트 통과 확인**

Run: `ctest --preset linux-dev -R ScenarioRunnerTest`

Expected: 모든 `ScenarioRunnerTest` PASS, hang 없음.

- [ ] **Step 8: 커밋**

```bash
git add apps/load-scenario-runner tests/server-net-linux
git commit -m "기능: 단일 방 broadcast 부하 시나리오 추가"
```

### Task 5: 다중 방 격리 시나리오

**Files:**
- Modify: `apps/load-scenario-runner/src/ScenarioRunner.cpp`
- Modify: `tests/server-net-linux/ScenarioRunnerTest.cpp`

**Interfaces:**
- Extends: `ScenarioRunner::runOnce()`의 `ScenarioKind::MultiRoom` 분기
- Consumes: `expectedBroadcasts(room_sizes, successful_sends_by_room)`

- [ ] **Step 1: 다중 방 실패 테스트 작성**

```cpp
TEST(ScenarioRunnerTest, MultiRoomKeepsBroadcastsInsideEachRoom) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::MultiRoom;
  options.clients = 4;
  options.rooms = 2;
  options.messages_per_sender = 2;
  options.payload_bytes = 128;

  const auto result = rss::tools::ScenarioRunner{}.runOnce(options, 1);
  EXPECT_EQ(result.sent, 8U);
  EXPECT_EQ(result.expected_broadcasts, 16U);
  EXPECT_EQ(result.received_broadcasts, 16U);
  EXPECT_EQ(result.missing_broadcasts, 0U);
  EXPECT_EQ(result.unexpected_broadcasts, 0U);
}
```

- [ ] **Step 2: 테스트가 올바른 이유로 실패하는지 확인**

Run: `cmake --build --preset linux-dev --target rss_server_net_tests && ctest --preset linux-dev -R ScenarioRunnerTest.MultiRoomKeepsBroadcastsInsideEachRoom`

Expected: multi-room 분기가 없어 FAIL.

- [ ] **Step 3: round-robin 방 준비와 허용 발신자 검사 구현**

클라이언트 `index % rooms`를 방 index로 사용한다. 방마다 첫 클라이언트가
생성한 실제 `room_id`를 저장하고 같은 방 클라이언트만 참가시킨다. receiver는
payload의 sender index가 자신의 방 구성원인지 검사하고 아니면
`unexpected_broadcasts`를 증가시킨다.

- [ ] **Step 4: 다중 방 테스트 통과 확인**

Run: `ctest --preset linux-dev -R ScenarioRunnerTest.MultiRoomKeepsBroadcastsInsideEachRoom`

Expected: PASS.

- [ ] **Step 5: 전체 Linux 회귀 테스트 확인**

Run: `ctest --preset linux-dev`

Expected: 기존 및 새 Linux tests 모두 PASS.

- [ ] **Step 6: 커밋**

```bash
git add apps/load-scenario-runner/src/ScenarioRunner.cpp tests/server-net-linux/ScenarioRunnerTest.cpp
git commit -m "기능: 다중 방 부하 시나리오 추가"
```

### Task 6: 느린 클라이언트 격리 시나리오

**Files:**
- Modify: `apps/load-scenario-runner/src/ScenarioRunner.h`
- Modify: `apps/load-scenario-runner/src/ScenarioRunner.cpp`
- Modify: `tests/server-net-linux/ScenarioRunnerTest.cpp`

**Interfaces:**
- Extends: `ScenarioRunner::runOnce()`의 `ScenarioKind::SlowClient` 분기
- Consumes: `ScenarioClient::setReceiveBufferBytes(1024)`
- Produces: slow client를 제외한 fast client 수신 결과와 `slow_client_disconnects`

- [ ] **Step 1: slow-client 격리 실패 테스트 작성**

```cpp
TEST(ScenarioRunnerTest,
     DefaultSlowClientLimitDisconnectsSlowClientWithoutFastErrors) {
  rss::tools::ScenarioOptions options;
  options.scenario = rss::tools::ScenarioKind::SlowClient;
  options.clients = 3;
  options.slow_clients = 1;
  options.messages_per_sender = 2000;
  options.payload_bytes = 4000;
  options.worker_count = 2;

  const auto result = rss::tools::ScenarioRunner{}.runOnce(options, 1);
  EXPECT_GE(result.overload.slow_client_disconnects, 1U);
  EXPECT_LE(result.overload.max_session_pending_write_bytes, 32U * 1024U);
  EXPECT_EQ(result.missing_broadcasts, 0U);
  EXPECT_EQ(result.duplicate_broadcasts, 0U);
  EXPECT_EQ(result.failed_clients, 0U);
}
```

- [ ] **Step 2: 테스트가 slow-client 분기 부재로 실패하는지 확인**

Run: `cmake --build --preset linux-dev --target rss_server_net_tests && ctest --preset linux-dev -R ScenarioRunnerTest.DefaultSlowClientLimitDisconnectsSlowClientWithoutFastErrors`

Expected: slow clients가 계속 수신하거나 종료 통계가 0이라 FAIL.

- [ ] **Step 3: 테스트 가능한 서버 조정 계약 구현**

```cpp
struct ScenarioTuning {
  std::size_t max_pending_write_bytes{1024U * 1024U};
  std::size_t slow_client_max_pending_write_bytes{32U * 1024U};
  int socket_receive_buffer_bytes{1024};
  std::size_t max_sessions{10000};
  std::chrono::milliseconds scenario_timeout{30s};
};

class ScenarioRunner {
 public:
  explicit ScenarioRunner(ScenarioTuning tuning = {});
  ScenarioRunResult runOnce(const ScenarioOptions& options,
                            std::size_t run_id) const;
};
```

slow-client 분기는 뒤쪽 `slow_clients`개에 작은 receive buffer를 요청하고
준비 이후에는 `recv`를 호출하지 않는다. fast clients만 발신·수신 작업을
수행한다. 서버 snapshot에서 slow 종료 수가 목표에 도달하거나 메시지 한도와
deadline이 끝날 때까지 실행한다. 일반 시나리오의 pending write 기본값은
1 MiB를 유지하고 slow-client에만 32 KiB 기본값을 적용한다.

- [ ] **Step 4: slow-client 테스트 통과 확인**

Run: `ctest --preset linux-dev -R ScenarioRunnerTest.DefaultSlowClientLimitDisconnectsSlowClientWithoutFastErrors --output-on-failure`

Expected: PASS이며 fast client 누락 0.

- [ ] **Step 5: 반복 안정성 확인**

Run: `ctest --preset linux-dev -R ScenarioRunnerTest.DefaultSlowClientLimitDisconnectsSlowClientWithoutFastErrors --repeat until-fail:10 --output-on-failure`

Expected: 10회 모두 PASS.

- [ ] **Step 6: 커밋**

```bash
git add apps/load-scenario-runner tests/server-net-linux/ScenarioRunnerTest.cpp
git commit -m "기능: 느린 클라이언트 격리 시나리오 추가"
```

### Task 7: 환경 정보, 실행 파일, 반복 출력

**Files:**
- Modify: `apps/load-scenario-runner/CMakeLists.txt`
- Create: `apps/load-scenario-runner/src/EnvironmentInfo.h`
- Create: `apps/load-scenario-runner/src/EnvironmentInfo.cpp`
- Create: `apps/load-scenario-runner/src/Program.h`
- Create: `apps/load-scenario-runner/src/Program.cpp`
- Create: `apps/load-scenario-runner/src/main.cpp`
- Create: `tests/server-net-linux/EnvironmentInfoTest.cpp`
- Modify: `tests/server-net-linux/CMakeLists.txt`

**Interfaces:**
- Produces: `EnvironmentInfo collectEnvironmentInfo()`, `formatEnvironment()`
- Produces: executable `rss_load_scenario_runner`
- Consumes: `parseScenarioOptions()`, `ScenarioRunner::runOnce()`, `formatRunResult()`, `isSuccessful()`

- [ ] **Step 1: 환경 출력 실패 테스트 작성**

```cpp
TEST(EnvironmentInfoTest, FormatsStableKeyValueOrder) {
  const rss::tools::EnvironmentInfo info{
      .commit = "abc1234", .os = "Linux", .kernel = "6.8",
      .cpu = "Example_CPU", .compiler = "Clang_18",
      .build_type = "Release", .workers = 4,
      .requested_slow_receive_buffer_bytes = 2048};
  EXPECT_EQ(rss::tools::formatEnvironment(info),
            "environment commit=abc1234 os=Linux kernel=6.8 "
            "cpu=Example_CPU compiler=Clang_18 build_type=Release "
            "workers=4 requested_slow_receive_buffer_bytes=2048");
}
```

- [ ] **Step 2: 환경 테스트 compile 실패 확인**

Run: `cmake --build --preset linux-dev --target rss_server_net_tests`

Expected: `EnvironmentInfo.h`가 없어 compile 실패.

- [ ] **Step 3: 환경 수집과 안정된 포맷 구현**

```cpp
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

EnvironmentInfo collectEnvironmentInfo(
    std::size_t workers, int requested_slow_receive_buffer_bytes);
std::string formatEnvironment(const EnvironmentInfo& info);
```

CMake configure 시 `git rev-parse --short HEAD`가 성공하면
`RSS_GIT_COMMIT`, `CMAKE_CXX_COMPILER_ID`와 버전으로 `RSS_COMPILER`,
`CMAKE_BUILD_TYPE`으로 `RSS_BUILD_TYPE` compile definition을 만든다. Git이
없으면 commit만 `unknown`으로 둔다. 공백은 `_`로 바꿔 key=value 토큰을
유지한다.

- [ ] **Step 4: 환경 테스트 통과 확인**

Run: `cmake --build --preset linux-dev --target rss_server_net_tests && ctest --preset linux-dev -R EnvironmentInfoTest`

Expected: PASS.

- [ ] **Step 5: main의 실패 테스트 가능한 경계 작성**

`main()`은 아래 함수만 호출하고 예외·종료 코드를 한곳에서 매핑한다.

```cpp
int runScenarioProgram(std::span<const std::string_view> args,
                       std::ostream& out, std::ostream& err);
```

단위 테스트에서 잘못된 인자가 종료 코드 `2`와 사용법을 반환하는지 먼저
검증한 후 구현한다.

- [ ] **Step 6: warm-up과 반복 실행 구현**

`runScenarioProgram()`은 환경 한 줄을 출력하고, 결과를 버리는 warm-up을
한 번 수행한 뒤 `repeats`회 새 서버로 `runOnce()`를 실행한다. 각 반복은
`formatRunResult()` 한 줄을 출력한다. 하나라도 `isSuccessful()`이 false면
전체 종료 코드 `1`, 인자 오류 `2`, 실행 예외 `3`을 반환한다.

- [ ] **Step 7: 실행 파일 smoke test**

Run: `./build/linux-dev/rss_load_scenario_runner --scenario broadcast --clients 2 --messages 2 --payload-bytes 128 --repeat 1 --workers 1`

Expected: environment 1줄과 run 1줄, `missing=0`, `duplicates=0`,
`unexpected=0`, 종료 코드 0.

- [ ] **Step 8: 커밋**

```bash
git add apps/load-scenario-runner tests/server-net-linux
git commit -m "기능: 반복 가능한 부하 시나리오 실행기 추가"
```

### Task 8: 계획 상태와 사용 문서 갱신

**Files:**
- Modify: `docs/development-plan.md`
- Modify: `docs/benchmark.md`
- Modify: `docs/design/2026-08-18-load-scenarios.md`
- Modify: `docs/plans/2026-08-18-load-scenarios.md`
- Modify: `README.md`
- Modify: `CONTRIBUTING.md`

**Interfaces:**
- Documents: `rss_load_test_client`와 `rss_load_scenario_runner`의 구분
- Documents: 세 시나리오 명령, 출력 필드, 종료 코드, 비교 제약
- Documents: Linux 전용 실행기의 build target과 짧은 smoke 절차

- [ ] **Step 1: 개발 계획 상태를 실제 이력과 맞춤**

`docs/development-plan.md`의 2주차 상태를 `완료 (2026-08-01)`로 바꾸고,
3주차는 모든 구현·검증이 끝난 시점에 `완료 (2026-08-18)`로 바꾼다.

- [ ] **Step 2: 벤치마크 문서에 실행 명령 추가**

다음 세 명령을 실제 CLI와 동일하게 기록한다.

```bash
./build/linux-dev/rss_load_scenario_runner --scenario broadcast --clients 100 --messages 100 --repeat 5 --workers 4
./build/linux-dev/rss_load_scenario_runner --scenario multi-room --clients 100 --rooms 10 --messages 100 --repeat 5 --workers 4
./build/linux-dev/rss_load_scenario_runner --scenario slow-client --clients 20 --slow-clients 1 --messages 2000 --payload-bytes 4000 --repeat 5 --workers 4
```

- [ ] **Step 3: README에 두 부하 도구의 역할 추가**

원격 서버의 PING 지연은 `rss_load_test_client`, 로컬 실제 서버의 재현 가능한
시나리오와 내부 통계는 `rss_load_scenario_runner`가 담당한다고 설명한다.
Linux 전용이라는 조건과 결과 비교 시 같은 환경을 사용해야 한다는 주의를
포함한다.

- [ ] **Step 4: 문서와 CLI 일치 여부 검사**

Run: `rg -n "rss_load_scenario_runner|broadcast|multi-room|slow-client|payload-bytes" README.md docs/development-plan.md docs/benchmark.md apps/load-scenario-runner`

Expected: 세 문서의 명령과 실제 옵션 이름이 일치하며 이전의 “측정할 수
없음” 설명이 남지 않음.

- [ ] **Step 5: 커밋**

```bash
git add README.md CONTRIBUTING.md docs/development-plan.md docs/benchmark.md \
  docs/design/2026-08-18-load-scenarios.md \
  docs/plans/2026-08-18-load-scenarios.md
git commit -m "문서: 실제 서버 부하 시나리오 사용법 추가"
```

### Task 9: 전체 검증과 Linux 반복 측정 확인

**Files:**
- Modify only if verification reveals a defect; every defect starts with a failing regression test.

**Interfaces:**
- Verifies: core, Linux network, formatting, tidy, scenario smoke and repeatability

- [ ] **Step 1: 플랫폼 독립 build/test**

Run: `cmake --preset core-dev && cmake --build --preset core-dev --parallel && ctest --preset core-dev`

Expected: 모든 core tests PASS.

- [ ] **Step 2: Linux 전체 build/test**

Run: `cmake --preset linux-dev && cmake --build --preset linux-dev --parallel && ctest --preset linux-dev`

Expected: 모든 Linux tests PASS.

- [ ] **Step 3: format 검사**

Run: `cmake --build --preset linux-dev --target format-check`

Expected: exit 0, formatting error 없음.

- [ ] **Step 4: clang-tidy 검사**

Run: `cmake --build --preset linux-dev --target tidy-check`

Expected: target이 제공되는 환경에서는 exit 0. `run-clang-tidy`가 없는
환경이면 미실행 사유를 결과에 명시한다.

- [ ] **Step 5: 세 시나리오 smoke 실행**

Run:

```bash
./build/linux-dev/rss_load_scenario_runner --scenario broadcast --clients 4 --messages 10 --payload-bytes 128 --repeat 1 --workers 2
./build/linux-dev/rss_load_scenario_runner --scenario multi-room --clients 4 --rooms 2 --messages 10 --payload-bytes 128 --repeat 1 --workers 2
./build/linux-dev/rss_load_scenario_runner --scenario slow-client --clients 3 --slow-clients 1 --messages 2000 --payload-bytes 4000 --repeat 1 --workers 2
```

Expected: 세 명령 모두 종료 코드 0. 일반 시나리오는 missing/duplicates/
unexpected/failed_clients가 0이고 slow 시나리오는 slow_client_disconnects가
1 이상.

- [ ] **Step 6: 작업 트리와 커밋 범위 확인**

Run: `git status --short && git diff main...HEAD --check && git log --oneline main..HEAD`

Expected: 작업 트리 clean, whitespace 오류 없음, 설계·기능·문서 커밋만 존재.
