# 영구 사용자 로그인 구현 계획

> 구현자는 이 계획을 task 순서대로 실행하고 각 checkbox를 갱신합니다. 모든
> 동작 변경은 실패하는 테스트를 먼저 추가한 뒤 최소 구현으로 통과시킵니다.

**목표:** PostgreSQL에 저장된 영구 `UserId`를 이름 기반 개발용 로그인으로
복구하고, DB 요청 동안 I/O 및 worker 스레드를 막지 않는 비동기 처리 기반을
구축합니다.

**아키텍처:** `server-core`에 DB 중립적인 사용자 저장소 port와 deferred
handler completion을 추가합니다. 기본 내장 서버는 in-memory adapter를
사용하고, Linux `rss_server` 애플리케이션은 전용 DB executor에서 blocking
`libpq` 호출을 수행하는 PostgreSQL adapter를 조립합니다. 같은 session의
후속 event는 영속 작업 완료 전까지 bounded parking 상태에 두며 worker는 다른
session을 계속 처리합니다.

**기술 스택:** C++20, CMake 3.20+, GoogleTest, PostgreSQL 16 이상, `libpq`,
Linux `epoll`/`eventfd`

**설계 문서:** `docs/design/2026-08-27-persistence-architecture.md`

## 전역 제약

- `libs/server-core`는 `protocol`과 표준 라이브러리 외의 프로젝트 코드에
  의존하지 않습니다.
- PostgreSQL header, `PGconn`과 SQL은 `libs/server-persistence-postgres` 밖으로
  노출하지 않습니다.
- 소켓, `epoll` 등록과 실제 연결 종료는 I/O 스레드만 수행합니다.
- worker는 DB 응답을 기다리거나 소켓을 직접 조작하지 않습니다.
- DB, handler 또는 요청 실패는 정상 session이나 server process 종료로
  확산하지 않습니다.
- handler 자체의 예상하지 못한 예외는 기존 정책대로 실패한 session만
  격리합니다.
- queue와 session별 parked event에는 명시적인 상한을 둡니다.
- 이름 로그인은 개발용 식별 절차이며 비밀번호나 인증 token을 추가하지
  않습니다.
- 사용자 ID wire 표현은 canonical lowercase UUID 문자열
  `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`입니다.
- 프로토콜 변경에는 `docs/protocol.md`와 server 및 client 테스트를 같이
  수정합니다.
- 실행 설정과 의존성 변경에는 `README.md`와 `CONTRIBUTING.md`를 같이
  수정합니다.
- public include는 `rss/...` 형식을 유지합니다.
- C++ 변경은 `format-check`, 가능한 `tidy-check`, 관련 build와 test를
  통과해야 합니다.

## 이번 계획의 범위

이 계획은 설계의 첫 번째 독립 배포 단위입니다.

- 포함: 비동기 handler completion, 영구 `UserId`, 사용자 repository,
  PostgreSQL `users` migration, 이름 기반 로그인, 재접속 user id 복구
- 제외: 영구 방과 다대다 membership, message 저장, read cursor, Cassandra와
  ScyllaDB adapter

제외 항목은 이 기반이 병합된 뒤 각각 별도 구현 계획으로 작성합니다. 현재
메모리 방의 수명과 한 session당 한 active room 규칙은 이번 단계에서 유지합니다.

## 파일 구조

### 새 server-core 파일

- `libs/server-core/include/rss/domain/UserId.h`: UUID 값 타입, parse와 format
- `libs/server-core/src/domain/UserId.cpp`: UUID canonical codec
- `libs/server-core/include/rss/persistence/PersistenceError.h`: 저장소 중립 오류
- `libs/server-core/include/rss/persistence/UserRecord.h`: 영구 사용자 record
- `libs/server-core/include/rss/persistence/UserRepository.h`: 비동기 사용자 port
- `libs/server-core/include/rss/persistence/InMemoryUserRepository.h`: 기본 adapter
- `libs/server-core/src/persistence/InMemoryUserRepository.cpp`: thread-safe 구현
- `tests/server-core/FakeSessionEventContext.h`: router 단위 테스트용 즉시 completion
  context

### 새 PostgreSQL adapter 파일

- `libs/server-persistence-postgres/CMakeLists.txt`: `libpq` adapter target
- `libs/server-persistence-postgres/include/rss/persistence/postgres/PostgresExecutor.h`:
  bounded DB task executor
- `libs/server-persistence-postgres/src/PostgresExecutor.cpp`: connection별 worker와
  lifecycle
- `libs/server-persistence-postgres/include/rss/persistence/postgres/PostgresUserRepository.h`:
  사용자 repository 구현
- `libs/server-persistence-postgres/src/PostgresUserRepository.cpp`: parameterized SQL,
  UUID decode와 오류 변환
- `libs/server-persistence-postgres/migrations/001_users.sql`: schema version과 users
  table
- `tests/server-persistence-postgres/CMakeLists.txt`: integration test target
- `tests/server-persistence-postgres/PostgresUserRepositoryTest.cpp`: 실제 PostgreSQL
  계약 테스트

### 수정 파일

- `libs/server-core/include/rss/service/Command.h`: deferred completion 내부 event
- `libs/server-core/include/rss/service/SessionEventHandler.h`: event context 계약
- `libs/server-core/include/rss/net/WorkerPool.h`
- `libs/server-core/src/net/WorkerPool.cpp`: non-blocking session sequencing
- `libs/server-core/include/rss/net/ServerConfig.h`: parked event 상한
- `libs/server-core/include/rss/domain/User.h`: `UserId` 적용
- `libs/server-core/include/rss/service/RoomService.h`
- `libs/server-core/src/service/RoomService.cpp`: 영구 사용자 session 연결
- `libs/server-core/include/rss/service/MessageRouter.h`
- `libs/server-core/src/service/MessageRouter.cpp`: repository 기반 deferred login
- `libs/server-core/CMakeLists.txt`
- `libs/server-net-linux/include/rss/net/TcpServer.h`
- `libs/server-net-linux/src/TcpServer.cpp`: 기본 in-memory adapter 조립
- `apps/server/src/main.cpp`: PostgreSQL adapter 조립과 안전한 종료 순서
- root 및 test `CMakeLists.txt`, `cmake/Dependencies.cmake`, `CMakePresets.json`
- `.github/workflows/ci.yml`: PostgreSQL integration CI
- server-core, server-net-linux, Qt client test의 user id 기대값
- `docs/protocol.md`, `docs/architecture.md`, `README.md`, `CONTRIBUTING.md`,
  `docs/project-status.md`

---

### Task 1: Deferred handler completion과 worker 비차단 순서 제어

**Files:**

- Modify: `libs/server-core/include/rss/service/Command.h`
- Modify: `libs/server-core/include/rss/service/SessionEventHandler.h`
- Modify: `libs/server-core/include/rss/net/WorkerPool.h`
- Modify: `libs/server-core/src/net/WorkerPool.cpp`
- Modify: `libs/server-core/include/rss/net/ServerConfig.h`
- Modify: `libs/server-core/src/service/MessageRouter.cpp`
- Modify: `tests/server-core/WorkerPoolNotificationTest.cpp`
- Modify: `tests/server-core/ServerConfigTest.cpp`
- Create: `tests/server-core/FakeSessionEventContext.h`

**Interfaces:**

- Produces:
  `std::shared_ptr<DeferredSessionCompletion> SessionEventContext::defer()`
- Produces:
  `bool DeferredSessionCompletion::succeed(std::vector<OutboundMessage>)`
- Produces: `bool DeferredSessionCompletion::fail()`
- Produces:
  `void SessionEventHandler::handle(const SessionEvent&, SessionEventContext&)`
- Produces: `ServerConfig::max_parked_events_per_session`, default `32`

- [ ] **Step 1: deferred event가 worker를 점유하지 않는 실패 테스트 작성**

`WorkerPoolNotificationTest.cpp`에 첫 session handler가 completion을 보관하고,
다른 session의 `PING` 성격 event가 먼저 처리되는 사례를 추가합니다.

```cpp
class DeferredFirstHandler final : public SessionEventHandler {
 public:
  void handle(const SessionEvent& event, SessionEventContext& context) override {
    if (event.session_id == 1 && !completion_) {
      completion_ = context.defer();
      return;
    }
    handled_sessions_.push_back(event.session_id);
  }

  std::shared_ptr<DeferredSessionCompletion> completion_;
  std::vector<std::uint64_t> handled_sessions_;
};

TEST(WorkerPoolNotificationTest, DeferredSessionDoesNotOccupyOnlyWorker) {
  // worker_count=1로 session 1 event와 session 2 event를 넣습니다.
  // session 1 completion 전에도 session 2가 처리되어야 합니다.
}
```

- [ ] **Step 2: 새 테스트가 현재 동기 handler 계약 때문에 compile 실패하는지 확인**

Run:

```bash
cmake --preset core-dev
cmake --build --preset core-dev --target rss_server_core_tests --parallel
```

Expected: `SessionEventContext`와 `DeferredSessionCompletion`이 정의되지 않아
compile 실패합니다.

- [ ] **Step 3: deferred completion command 계약 추가**

`Command.h`에서 `OutboundMessage`를 `SessionEvent`보다 먼저 정의하고 내부 완료
payload를 추가합니다.

```cpp
enum class SessionEventKind {
  Packet,
  Disconnected,
  DeferredCompletion,
};

struct DeferredCompletionPayload {
  bool failed{};
  std::vector<OutboundMessage> messages;
};

struct SessionEvent {
  SessionEventKind kind{SessionEventKind::Packet};
  std::uint64_t session_id{};
  protocol::Packet packet;
  std::uint64_t sequence{};
  std::shared_ptr<DeferredCompletionPayload> completion;
};
```

`SessionEventHandler.h`에는 다음 계약을 추가합니다.

```cpp
class DeferredSessionCompletion {
 public:
  virtual ~DeferredSessionCompletion() = default;
  [[nodiscard]] virtual bool succeed(
      std::vector<OutboundMessage> messages) = 0;
  [[nodiscard]] virtual bool fail() = 0;
};

class SessionEventContext : public OutboundMessageSink {
 public:
  [[nodiscard]] virtual std::shared_ptr<DeferredSessionCompletion> defer() = 0;
};

class SessionEventHandler {
 public:
  virtual ~SessionEventHandler() = default;
  virtual void handle(const SessionEvent& event,
                      SessionEventContext& context) = 0;
};
```

`FakeSessionEventContext.h`는 `emit()` 메시지를 vector에 모으고 `defer()`가
반환한 test completion의 `succeed()` 메시지도 같은 vector에 추가합니다.
`fail()`은 `failed` flag를 한 번만 설정합니다. 이 fake는 inline repository
callback을 사용하는 router 테스트에서 공유합니다.

completion은 exactly-once atomic guard를 사용하고, 성공 또는 실패 내부 event를
원래 `session_id`와 `sequence`로 inbox에 `push`합니다. 닫힌 queue에는
`false`를 반환하고 접근하지 않습니다.

- [ ] **Step 4: WorkerPool session state를 active/waiting 방식에서 bounded parking으로 변경**

`SessionSequenceState`는 다음 상태를 유지합니다.

```cpp
struct SessionSequenceState {
  std::uint64_t next_sequence{};
  bool active{};
  bool awaiting_completion{};
  bool failed{};
  std::map<std::uint64_t, service::SessionEvent> parked;
};
```

worker는 순서가 아직 아닌 event를 condition variable에서 기다리지 않고
`parked`에 옮긴 뒤 다음 inbox event를 처리합니다. 일반 event 완료 시
`next_sequence`를 증가시키고 정확히 다음 sequence가 parked 상태면 같은 worker의
local loop에서 즉시 처리합니다. `DeferredCompletion`은 active이고
`awaiting_completion`인 동일 sequence에만 적용합니다.

handler가 `defer()`를 호출하면 active turn을 유지하고 worker loop로 돌아갑니다.
inline callback으로 completion event가 먼저 도착한 경우 state에 보관했다가
handler 반환 후 동일 worker가 처리합니다.

session별 parked 개수가 `max_parked_events_per_session`을 넘으면 기존 handler
exception과 동일하게 해당 session에 `DisconnectSession`만 게시합니다.
`ServerConfig` 값을 `WorkerPoolConfig`로 전달하며 0은 validation에서
거절합니다.

graceful `beginStop()`은 외부 입력 수락을 중단하되 outstanding deferred count가
0이 될 때까지 completion용 inbox를 닫지 않습니다. WorkerPool은
`drain_requested`, active turn 수와 outstanding deferred 수를 함께 추적하고,
inbox가 비고 세 값이 모두 drain 조건을 만족할 때 queue를 닫아 worker를
깨웁니다. `forceStop()`만 completion queue를 즉시 닫습니다.

- [ ] **Step 5: 정상 완료, 실패, inline callback과 shutdown 테스트 추가**

다음 이름의 테스트를 작성합니다.

```text
DeferredSessionDoesNotOccupyOnlyWorker
DeferredCompletionPublishesMessagesBeforeNextSequence
InlineDeferredCompletionDoesNotRaceHandlerReturn
DeferredFailureDisconnectsOnlyOwningSession
ParkedEventLimitDisconnectsOnlyOwningSession
GracefulStopWaitsForDeferredCompletion
ForceStopRejectsLateDeferredCompletion
```

`GracefulStopWaitsForDeferredCompletion`은 `beginStop()` 후 completion을 성공시키고
worker가 출력 게시와 join을 마치는지 검증합니다. `forceStop()`은 inbox를 즉시
닫아 late completion이 `false`를 반환하게 합니다.

- [ ] **Step 6: server-core 테스트 실행**

Run:

```bash
cmake --build --preset core-dev --target rss_server_core_tests --parallel
ctest --test-dir build/core-dev -R 'WorkerPoolNotificationTest|ServerConfigTest' --output-on-failure
```

Expected: 새 deferred, 순서, 상한과 shutdown 테스트가 모두 통과합니다.

- [ ] **Step 7: 첫 기반 커밋**

```bash
git add libs/server-core tests/server-core
git commit -m "기능: 비동기 handler 완료 흐름 추가"
```

---

### Task 2: 영구 UserId 값 타입과 사용자 저장소 port

**Files:**

- Create: `libs/server-core/include/rss/domain/UserId.h`
- Create: `libs/server-core/src/domain/UserId.cpp`
- Create: `libs/server-core/include/rss/persistence/PersistenceError.h`
- Create: `libs/server-core/include/rss/persistence/UserRecord.h`
- Create: `libs/server-core/include/rss/persistence/UserRepository.h`
- Create: `libs/server-core/include/rss/persistence/InMemoryUserRepository.h`
- Create: `libs/server-core/src/persistence/InMemoryUserRepository.cpp`
- Create: `tests/server-core/UserIdTest.cpp`
- Create: `tests/server-core/InMemoryUserRepositoryTest.cpp`
- Modify: `libs/server-core/CMakeLists.txt`
- Modify: `tests/server-core/CMakeLists.txt`

**Interfaces:**

- Produces: `UserId::parse(std::string_view) -> std::optional<UserId>`
- Produces: `UserId::toString() -> std::string`
- Produces:
  `UserRepository::findOrCreateByNormalizedName(FindOrCreateUser, UserCallback)`
- Callback rule: exactly once, 호출 스레드는 adapter가 결정하며 예외를 던지지 않음

- [ ] **Step 1: UUID canonical codec 실패 테스트 작성**

```cpp
TEST(UserIdTest, RoundTripsCanonicalUuid) {
  const auto id = UserId::parse("018f7f54-7c2a-7f31-8f0d-123456789abc");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(id->toString(), "018f7f54-7c2a-7f31-8f0d-123456789abc");
}

TEST(UserIdTest, RejectsNonCanonicalUuid) {
  EXPECT_FALSE(UserId::parse("018F7F54-7C2A-7F31-8F0D-123456789ABC"));
  EXPECT_FALSE(UserId::parse("not-a-uuid"));
}
```

- [ ] **Step 2: UUID 테스트 compile 실패 확인**

Run:

```bash
cmake --build --preset core-dev --target rss_server_core_tests --parallel
```

Expected: `rss/domain/UserId.h`가 없어 compile 실패합니다.

- [ ] **Step 3: `UserId` 최소 구현**

```cpp
class UserId {
 public:
  using Bytes = std::array<std::uint8_t, 16>;

  constexpr UserId() = default;
  explicit constexpr UserId(Bytes bytes) : bytes_(bytes) {}

  [[nodiscard]] static std::optional<UserId> parse(std::string_view text);
  [[nodiscard]] std::string toString() const;
  [[nodiscard]] constexpr const Bytes& bytes() const noexcept { return bytes_; }
  auto operator<=>(const UserId&) const = default;

 private:
  Bytes bytes_{};
};
```

parser는 길이 36, dash 위치 `8,13,18,23`, lowercase hex만 허용합니다.
formatter는 lowercase hex와 같은 dash 위치를 사용합니다.

- [ ] **Step 4: repository 계약과 오류 타입 테스트 작성**

```cpp
TEST(InMemoryUserRepositoryTest, ReusesIdForNormalizedName) {
  InMemoryUserRepository repository;
  std::vector<UserResult> results;
  repository.findOrCreateByNormalizedName(
      {"alice", "Alice"}, [&](UserResult result) {
        results.push_back(std::move(result));
      });
  repository.findOrCreateByNormalizedName(
      {"alice", "Alice"}, [&](UserResult result) {
        results.push_back(std::move(result));
      });
  ASSERT_EQ(results.size(), 2U);
  ASSERT_TRUE(results[0].user && results[1].user);
  EXPECT_EQ(results[0].user->user_id, results[1].user->user_id);
}
```

- [ ] **Step 5: 저장소 port와 in-memory adapter 구현**

```cpp
enum class PersistenceErrorKind {
  Busy,
  Unavailable,
  Timeout,
  Constraint,
  InvalidData,
  Stopping,
};

struct UserRecord {
  domain::UserId user_id;
  std::string normalized_name;
  std::string display_name;
};

struct FindOrCreateUser {
  std::string normalized_name;
  std::string display_name;
};

struct UserResult {
  std::optional<UserRecord> user;
  std::optional<PersistenceError> error;
};

using UserCallback = std::function<void(UserResult)>;

class UserRepository {
 public:
  virtual ~UserRepository() = default;
  virtual void findOrCreateByNormalizedName(FindOrCreateUser request,
                                             UserCallback callback) = 0;
};
```

in-memory adapter는 mutex로 name map을 보호하고 deterministic 16-byte ID의 마지막
8 byte에 증가값을 big-endian으로 기록합니다. callback은 lock을 해제한 뒤
호출해 재진입 deadlock을 막습니다.

- [ ] **Step 6: 새 값 타입과 adapter 테스트 실행**

Run:

```bash
cmake --build --preset core-dev --target rss_server_core_tests --parallel
ctest --test-dir build/core-dev -R 'UserIdTest|InMemoryUserRepositoryTest' --output-on-failure
```

Expected: UUID와 동일 이름 재사용 테스트가 통과합니다.

- [ ] **Step 7: 도메인 기반 커밋**

```bash
git add libs/server-core tests/server-core
git commit -m "기능: 영구 사용자 저장소 계약 추가"
```

---

### Task 3: repository 기반 영구 로그인과 UUID 프로토콜

**Files:**

- Modify: `libs/server-core/include/rss/domain/User.h`
- Modify: `libs/server-core/include/rss/service/RoomService.h`
- Modify: `libs/server-core/src/service/RoomService.cpp`
- Modify: `libs/server-core/include/rss/service/MessageRouter.h`
- Modify: `libs/server-core/src/service/MessageRouter.cpp`
- Modify: `libs/server-net-linux/include/rss/net/TcpServer.h`
- Modify: `libs/server-net-linux/src/TcpServer.cpp`
- Modify: `tests/server-core/RoomServiceTest.cpp`
- Modify: `tests/server-core/MessageRouterTest.cpp`
- Modify: `tests/server-core/WorkerPoolNotificationTest.cpp`
- Modify: `tests/server-net-linux/TcpServerBackpressureTest.cpp`
- Modify: `tests/server-net-linux/EmbeddedServerTest.cpp`
- Modify: `tests/qt-client/ClientControllerTest.cpp`
- Modify: `tests/qt-client/MainWindowTest.cpp`
- Modify: `benchmarks/MessageRouterBenchmark.cpp`
- Modify: `docs/protocol.md`

**Interfaces:**

- Consumes: Task 1 deferred completion과 Task 2 `UserRepository`
- Produces:
  `LoginResult RoomService::attachUser(SessionId, const UserRecord&)`
- Produces:
  `MessageRouter(RoomService&, persistence::UserRepository&)`
- Wire output:
  `OK|user_id=<canonical-uuid>|session_id=<number>|name=<display-name>`

- [ ] **Step 1: 재접속 user id 복구 실패 테스트 작성**

`MessageRouterTest.cpp`에서 같은 repository를 사용하는 두 session이 `alice`로
로그인하는 scenario를 추가합니다.

```cpp
TEST(MessageRouterTest, ReconnectWithSameNameRestoresPermanentUserId) {
  InMemoryUserRepository users;
  RoomService rooms;
  MessageRouter router(rooms, users);

  // session 1 LOGIN_REQ 처리 후 Disconnected 처리
  // session 2에서 같은 이름으로 LOGIN_REQ 처리
  // 두 LOGIN_RES의 user_id 문자열이 같아야 합니다.
}
```

기존 router 직접 호출은 `OutboundMessageSink` 대신
`FakeSessionEventContext`를 전달합니다. `MessageRouter::handlePacket`도 로그인
분기에서 `defer()`를 사용할 수 있도록 `SessionEventContext&`를 받으며, 기존
동기 packet 분기는 상위 타입인 `OutboundMessageSink&`로 helper를 호출할 수
있습니다. benchmark의 `CountingSink`는 `SessionEventContext`를 구현하고 chat
경로에서 호출되지 않는 `defer()`는 `std::logic_error`를 던지게 합니다.

- [ ] **Step 2: 기존 session별 숫자 ID 구현에서 테스트 실패 확인**

Run:

```bash
cmake --build --preset core-dev --target rss_server_core_tests --parallel
ctest --test-dir build/core-dev -R 'MessageRouterTest.ReconnectWithSameNameRestoresPermanentUserId' --output-on-failure
```

Expected: 두 로그인 응답의 user id가 달라 실패합니다.

- [ ] **Step 3: 사용자 이름 정규화 함수와 테스트 추가**

`MessageRouter.cpp`의 private helper는 ASCII 앞뒤 공백 제거, ASCII `A-Z` 소문자
변환과 1..32 byte 제한을 적용합니다. 빈 문자열은 기존 자동 이름으로 대체하지
않고 `invalid user name`으로 거절합니다. 비 ASCII byte는 변경하지 않습니다.

```cpp
struct NormalizedLoginName {
  std::string normalized;
  std::string display;
};

std::optional<NormalizedLoginName> normalizeLoginName(std::string_view input);
```

테스트는 `" Alice " -> {"alice", "Alice"}`, 빈 값과 33 byte 거절, 한글 UTF-8
표시 이름 보존을 검증합니다.

- [ ] **Step 4: `User`와 `RoomService`를 영구 사용자 연결 방식으로 변경**

```cpp
struct User {
  UserId id;
  std::uint64_t session_id{};
  std::string name;
};

LoginResult RoomService::attachUser(
    std::uint64_t session_id,
    const persistence::UserRecord& record);
```

`attachUser`는 이미 로그인된 session이면 기존 오류를 반환하고, 성공하면
repository record의 ID와 display name을 session 사용자로 저장합니다.
`next_user_id_`와 기존 `login()`은 제거합니다. disconnect는 session 연결만
지우며 repository record는 지우지 않습니다.

- [ ] **Step 5: `MessageRouter` 로그인만 deferred repository 호출로 변경**

```cpp
auto completion = context.defer();
user_repository_.findOrCreateByNormalizedName(
    persistence::FindOrCreateUser{normalized->normalized, normalized->display},
    [this, session_id, completion](persistence::UserResult result) {
      if (!result.user) {
        static_cast<void>(completion->succeed(
            {error(session_id, persistenceErrorText(*result.error))}));
        return;
      }
      const auto login = room_service_.attachUser(session_id, *result.user);
      static_cast<void>(completion->succeed(
          {login.ok ? loginResponse(session_id, login.user)
                    : error(session_id, login.error)}));
    });
```

`Busy`, `Unavailable`, `Timeout`, `Stopping`은 각각 안정된 protocol 문자열로
매핑하되 DB 상세 오류와 connection string은 payload에 포함하지 않습니다.
다른 packet 종류는 기존처럼 context에 동기 emit합니다.

- [ ] **Step 6: 기본 TcpServer에 in-memory adapter 조립**

`TcpServer` member 순서를 다음과 같이 유지합니다.

```cpp
persistence::InMemoryUserRepository in_memory_users_;
service::RoomService room_service_;
service::MessageRouter router_;
```

실제 namespace는 `rss::persistence`를 사용합니다. constructor는
`router_(room_service_, in_memory_users_)`로 조립해 embedded server와 기존
network test가 외부 DB 없이 동작하게 합니다. 외부 handler injection 계약은
유지합니다.

- [ ] **Step 7: UUID 출력에 맞춰 server와 Qt client 테스트 및 문서 갱신**

고정 테스트 ID는 `00000000-0000-0000-0000-000000000001`처럼 canonical 문자열을
사용합니다. Qt client는 현재 user id를 문자열 field로만 표시하므로 숫자 변환
가정이 있는지 확인하고 해당 가정만 제거합니다.

`docs/protocol.md`의 로그인, 방 event와 chat event 예시를 UUID user id로 바꾸고,
같은 이름 재접속은 같은 ID를 돌려준다는 규칙과 이름 로그인이 인증이 아님을
기록합니다.

- [ ] **Step 8: core와 Qt 회귀 테스트 실행**

Run:

```bash
cmake --build --preset core-dev --parallel
ctest --preset core-dev
cmake --preset qt-client-dev
cmake --build --preset qt-client-dev --parallel
ctest --preset qt-client-dev
```

Expected: server-core 전체와 Qt controller/widget 테스트가 통과합니다.

- [ ] **Step 9: 영구 로그인 도메인 커밋**

```bash
git add libs tests docs/protocol.md
git commit -m "기능: 영구 사용자 ID 로그인 적용"
```

---

### Task 4: PostgreSQL executor, migration과 UserRepository adapter

**Files:**

- Create: `libs/server-persistence-postgres/CMakeLists.txt`
- Create: `libs/server-persistence-postgres/include/rss/persistence/postgres/PostgresExecutor.h`
- Create: `libs/server-persistence-postgres/src/PostgresExecutor.cpp`
- Create: `libs/server-persistence-postgres/include/rss/persistence/postgres/PostgresUserRepository.h`
- Create: `libs/server-persistence-postgres/src/PostgresUserRepository.cpp`
- Create: `libs/server-persistence-postgres/migrations/001_users.sql`
- Create: `tests/server-persistence-postgres/CMakeLists.txt`
- Create: `tests/server-persistence-postgres/PostgresUserRepositoryTest.cpp`
- Modify: `CMakeLists.txt`
- Modify: `cmake/Dependencies.cmake`
- Modify: `tests/CMakeLists.txt`
- Modify: `CMakePresets.json`

**Interfaces:**

- Consumes: `persistence::UserRepository`
- Produces:
  `PostgresExecutor(std::string connection_string, std::size_t workers,
  std::size_t capacity)`
- Produces: `void PostgresExecutor::start()`, `void stop() noexcept`
- Produces:
  `PostgresUserRepository(PostgresExecutor&)`
- Environment for integration test: `RSS_TEST_DATABASE_URL`

- [ ] **Step 1: 실제 DB repository 계약 테스트 작성**

`PostgresUserRepositoryTest`는 환경 변수가 없으면 `GTEST_SKIP()`하고, 있으면 각
test마다 고유한 이름 prefix를 사용합니다.

```cpp
TEST_F(PostgresUserRepositoryTest, ConcurrentFindOrCreateReturnsOneUserId) {
  constexpr int kRequests = 16;
  // 16개 요청을 같은 normalized_name으로 동시에 submit합니다.
  // condition_variable로 16 callback을 기다립니다.
  // 모든 결과가 성공하고 UserId가 하나인지 검증합니다.
}

TEST_F(PostgresUserRepositoryTest, ReconnectFromNewRepositoryReusesUserId) {
  // 첫 executor/repository로 생성한 뒤 종료합니다.
  // 새 executor/repository로 같은 이름을 조회해 같은 ID인지 검증합니다.
}
```

추가로 queue capacity가 1일 때 `Busy`, 잘못된 connection에서 startup 실패,
`stop()` 이후 submit의 `Stopping` callback exactly-once를 검증합니다.

- [ ] **Step 2: PostgreSQL target 부재로 compile 실패 확인**

Run:

```bash
cmake --preset linux-dev
cmake --build --preset linux-dev --target rss_postgres_persistence_tests --parallel
```

Expected: target이 없어 실패합니다.

- [ ] **Step 3: 선택 가능한 PostgreSQL build 구성 추가**

Linux server build는 adapter를 기본으로 포함하고 플랫폼 독립 build에서는
제외하도록 root option을 다음과 같이 정의합니다.

```cmake
set(RSS_BUILD_POSTGRES_DEFAULT OFF)
if(RSS_BUILD_NETWORK_TARGETS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(RSS_BUILD_POSTGRES_DEFAULT ON)
endif()
option(RSS_BUILD_POSTGRES "Build PostgreSQL persistence adapter"
       ${RSS_BUILD_POSTGRES_DEFAULT})

if(RSS_BUILD_POSTGRES)
    find_package(PostgreSQL REQUIRED)
    add_subdirectory(libs/server-persistence-postgres)
endif()
```

`linux-dev` preset은 `RSS_BUILD_POSTGRES=ON`, `core-dev`, benchmark와 Qt preset은
`OFF`를 명시합니다. 일반 Linux network configure도 기본값 `ON`을 사용합니다.
adapter target은 `rss_server_core`,
`PostgreSQL::PostgreSQL`, `Threads::Threads`에 의존합니다.

- [ ] **Step 4: 첫 migration 작성**

```sql
BEGIN;

CREATE TABLE IF NOT EXISTS schema_migrations (
    version bigint PRIMARY KEY,
    applied_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS users (
    user_id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    normalized_name varchar(32) NOT NULL UNIQUE,
    display_name varchar(32) NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

INSERT INTO schema_migrations(version)
VALUES (1)
ON CONFLICT (version) DO NOTHING;

COMMIT;
```

migration은 반복 적용 가능해야 하며 데이터나 table을 삭제하는 문장을 포함하지
않습니다.

- [ ] **Step 5: bounded PostgreSQL executor 구현**

```cpp
class PostgresExecutor {
 public:
  using Task = std::function<void(PGconn*)>;

  PostgresExecutor(std::string connection_string,
                   std::size_t worker_count,
                   std::size_t queue_capacity);
  ~PostgresExecutor() noexcept;

  void start();
  [[nodiscard]] bool submit(Task task);
  void stop() noexcept;
};
```

header에서는 `struct pg_conn;`만 전방 선언하고 public task signature도
`pg_conn*`를 사용해 `libpq-fe.h` 노출을 피합니다. `start()`는 각 worker
connection을 모두 검증한 뒤 성공하며
하나라도 실패하면 생성한 connection을 정리하고 예외를 던집니다. 각 worker는
자기 `PGconn`만 사용합니다. queue는 `BoundedBlockingQueue<Task>`로 제한하고
`submit`은 기다리지 않는 `tryPush`를 사용합니다.

connection string, password와 원본 `PQerrorMessage`는 callback payload나 일반
로그에 포함하지 않습니다. SQL에는 `statement_timeout`을 적용하고 broken
connection은 다음 task 전에 `PQreset`으로 한 번 복구합니다.

- [ ] **Step 6: parameterized UserRepository SQL 구현**

insert는 문자열 결합 없이 `PQexecParams`를 사용합니다.

```sql
WITH inserted AS (
  INSERT INTO users(normalized_name, display_name)
  VALUES ($1, $2)
  ON CONFLICT (normalized_name) DO NOTHING
  RETURNING user_id::text, normalized_name, display_name
)
SELECT user_id, normalized_name, display_name FROM inserted
UNION ALL
SELECT user_id::text, normalized_name, display_name
FROM users
WHERE normalized_name = $1
LIMIT 1;
```

동시 transaction visibility 때문에 결과 행이 0개면 같은 connection에서
parameterized `SELECT`를 제한 횟수 1회 다시 수행합니다. 여전히 없으면
`Unavailable`을 반환합니다. UUID parse 실패는 `InvalidData`, queue 거절은
`Busy`, SQLSTATE class `23`은 `Constraint`, connection 오류는 `Unavailable`로
매핑합니다. callback은 executor task 바깥으로 예외를 내보내지 않고 정확히 한
번 호출합니다.

- [ ] **Step 7: migration을 적용한 실제 PostgreSQL 테스트 실행**

Run:

```bash
psql "$RSS_TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f libs/server-persistence-postgres/migrations/001_users.sql
cmake --build --preset linux-dev --target rss_postgres_persistence_tests --parallel
ctest --test-dir build/linux-dev -R PostgresUserRepositoryTest --output-on-failure
```

Expected: 동시 생성, process 재구성 후 ID 복구, queue 포화, stopping과 연결 오류
테스트가 통과합니다.

- [ ] **Step 8: PostgreSQL adapter 커밋**

```bash
git add CMakeLists.txt CMakePresets.json cmake libs/server-persistence-postgres tests
git commit -m "기능: PostgreSQL 사용자 저장소 추가"
```

---

### Task 5: 실제 서버 조립, 설정, CI와 운영 문서

**Files:**

- Modify: `apps/server/CMakeLists.txt`
- Modify: `apps/server/src/main.cpp`
- Modify: `.github/workflows/ci.yml`
- Modify: `README.md`
- Modify: `CONTRIBUTING.md`
- Modify: `docs/architecture.md`
- Modify: `docs/project-status.md`
- Modify: `tests/server-net-linux/ServerSignalTest.cpp`
- Create: `tests/server-net-linux/PostgresServerLoginTest.cpp`

**Interfaces:**

- Consumes: `PostgresExecutor`, `PostgresUserRepository`, `MessageRouter`
- Produces environment:
  `RSS_DATABASE_URL`, `RSS_DB_WORKERS`, `RSS_DB_QUEUE_CAPACITY`
- Default values: DB workers `2`, DB queue capacity `1024`
- Production `rss_server`: `RSS_DATABASE_URL` 필수

- [ ] **Step 1: server process 재시작 로그인 통합 테스트 작성**

`PostgresServerLoginTest`는 `RSS_TEST_DATABASE_URL`이 없으면 skip합니다. 임시
port에서 첫 server process를 실행해 `alice` 로그인 ID를 받은 뒤 정상 종료하고,
같은 DB로 새 process를 실행해 같은 ID가 반환되는지 검증합니다.

```text
start server A -> LOGIN_REQ alice -> capture UUID -> SIGTERM -> exit 0
start server B -> LOGIN_REQ alice -> capture UUID -> expect same UUID
```

DB가 중단된 경우 로그인 요청만 `database unavailable`로 실패하고 이미 연결된
다른 session의 `PING`은 `PONG`을 받는 scenario도 추가합니다.

- [ ] **Step 2: 현재 server가 in-memory ID를 반환해 재시작 테스트가 실패하는지 확인**

Run:

```bash
cmake --build --preset linux-dev --target rss_server_net_tests --parallel
ctest --test-dir build/linux-dev -R PostgresServerLoginTest --output-on-failure
```

Expected: PostgreSQL server 조립 또는 환경 설정이 없어 실패합니다.

- [ ] **Step 3: `rss_server`에 PostgreSQL adapter 조립**

`main.cpp`는 환경 변수를 검증한 뒤 다음 lifetime 순서를 유지합니다.

```cpp
PostgresExecutor executor(database_url, db_workers, db_queue_capacity);
executor.start();
PostgresUserRepository users(executor);
RoomService rooms;
MessageRouter router(rooms, users);

{
  TcpServer server(config, &router);
  ShutdownSignalMonitor signal_monitor(shutdown_signals, server);
  try {
    server.run();
  } catch (...) {
    executor.stop();  // completion 대상 객체가 살아 있을 때 DB thread join
    throw;
  }
  executor.stop();
  signal_monitor.throwIfFailed();
}
```

실제 구현에서는 `signal_monitor.throwIfFailed()`의 기존 안전한 순서를 보존하고,
executor가 멈출 때까지 `router`, `rooms`와 `server` completion queue가 살아 있게
scope를 배치합니다. DB 시작 실패는 listener를 열기 전에 process 시작 실패로
처리합니다.

- [ ] **Step 4: CI PostgreSQL service와 패키지 추가**

Ubuntu `build-and-test`와 sanitizer job에 `libpq-dev postgresql-client`를 설치하고
다음 service를 사용합니다. `code-quality` job에도 `libpq-dev`를 설치해
PostgreSQL adapter까지 format과 tidy 대상에 포함합니다.

```yaml
services:
  postgres:
    image: postgres:16
    env:
      POSTGRES_USER: rss
      POSTGRES_PASSWORD: rss_test
      POSTGRES_DB: rss_test
    ports:
      - 5432:5432
    options: >-
      --health-cmd "pg_isready -U rss -d rss_test"
      --health-interval 5s
      --health-timeout 5s
      --health-retries 10
```

job environment는
`RSS_TEST_DATABASE_URL=postgresql://rss:rss_test@127.0.0.1:5432/rss_test`를
사용합니다. test 전에 `001_users.sql`을 `ON_ERROR_STOP=1`로 적용합니다.
macOS, Windows core와 Qt job은 PostgreSQL adapter를 build하지 않습니다.

- [ ] **Step 5: 실행·개발·구조 문서 갱신**

`README.md`와 `CONTRIBUTING.md`에 Ubuntu `libpq-dev`, PostgreSQL 16, migration
적용 명령, 세 환경 변수와 server 실행 예시를 추가합니다.

```bash
export RSS_DATABASE_URL='postgresql://rss:password@127.0.0.1:5432/rss'
psql "$RSS_DATABASE_URL" -v ON_ERROR_STOP=1 \
  -f libs/server-persistence-postgres/migrations/001_users.sql
./build/linux-dev/rss_server 0.0.0.0 7777 4
```

실제 문서 예시에는 production credential을 넣지 않습니다.
`docs/architecture.md`에는 DB executor, deferred completion, session parking 상한과
DB 장애 격리를 추가합니다. `docs/project-status.md`에는 영구 사용자 로그인만
완료로 표시하고 방·메시지 영속화는 다음 우선순위로 남깁니다.

- [ ] **Step 6: Linux 통합 및 process 테스트 실행**

Run:

```bash
cmake --preset linux-dev
cmake --build --preset linux-dev --parallel
ctest --preset linux-dev
```

Expected: PostgreSQL repository, server 재시작 로그인, signal shutdown과 기존
Linux network 테스트가 모두 통과합니다.

- [ ] **Step 7: 서버 조립과 문서 커밋**

```bash
git add apps .github README.md CONTRIBUTING.md docs tests/server-net-linux
git commit -m "기능: 서버에 PostgreSQL 영구 로그인 연결"
```

---

### Task 6: 전체 회귀, 정적 검사와 PR 준비

**Files:**

- Modify only if verification exposes a defect in files already changed by Tasks 1-5

**Interfaces:**

- Consumes: Tasks 1-5의 전체 영구 로그인 동작
- Produces: review 가능한 단일 feature branch

- [ ] **Step 1: 변경 범위와 금칙 항목 확인**

Run:

```bash
git status --short
git diff origin/main...HEAD --check
git diff origin/main...HEAD --stat
rg -n 'BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY|postgresql://[^[:space:]]+:[^[:space:]]+@' docs README.md CONTRIBUTING.md libs apps tests .github
```

Expected: 의도한 파일만 변경되고 공백 오류, placeholder와 실제 credential이
없습니다. test fixture의 `rss_test` credential은 CI 전용임을 명시한 위치만
허용합니다.

- [ ] **Step 2: format 적용 후 format-check**

Run:

```bash
cmake --build --preset linux-dev --target format
cmake --build --preset linux-dev --target format-check
```

Expected: format 적용 뒤 `format-check`가 exit 0입니다. format이 파일을
수정했다면 관련 task의 파일만 포함되는지 diff를 다시 확인하고 별도
`스타일: 영속성 코드 포맷 정리` 커밋을 만듭니다.

- [ ] **Step 3: 전체 Linux build와 test**

Run:

```bash
cmake --build --preset linux-dev --parallel
ctest --preset linux-dev
```

Expected: build exit 0, CTest failed test 0개입니다.

- [ ] **Step 4: 플랫폼 독립 core와 Qt 회귀 검증**

Run:

```bash
cmake --preset core-dev
cmake --build --preset core-dev --parallel
ctest --preset core-dev
cmake --preset qt-client-dev
cmake --build --preset qt-client-dev --parallel
ctest --preset qt-client-dev
```

Expected: PostgreSQL library 없이 core와 Qt build/test가 통과합니다.

- [ ] **Step 5: clang-tidy와 benchmark dry-run**

Run:

```bash
cmake --build --preset linux-dev --target tidy-check
cmake --preset benchmark
cmake --build --preset benchmark --target rss_microbenchmarks --parallel
./build/benchmark/rss_microbenchmarks --benchmark_dry_run
```

Expected: tidy exit 0이고 모든 benchmark가 실행 가능합니다.

- [ ] **Step 6: 최종 diff와 commit 상태 확인**

Run:

```bash
git status --short --branch
git log --oneline --decorate origin/main..HEAD
git diff origin/main...HEAD --check
```

Expected: worktree가 깨끗하고 계획된 한글 commit만 있으며 diff check가 exit
0입니다.

## 후속 계획 경계

이 PR이 병합되고 production-style 재시작 test가 통과한 뒤 다음 순서로 별도
계획을 작성합니다.

1. PostgreSQL `rooms`와 `room_memberships`, 한 사용자 여러 방 가입
2. `MessageStore` PostgreSQL 구현, `client_message_id` 멱등성과 저장 후 broadcast
3. 최근 메시지 page, `room_read_cursors`와 재접속 복구
4. 실측 부하 결과에 따른 Cassandra/ScyllaDB 비교 adapter

각 후속 계획은 앞 단계의 공개 port와 migration version만 의존하며, 동시에
여러 저장 모델을 한 PR에서 바꾸지 않습니다.
