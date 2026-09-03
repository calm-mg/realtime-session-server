# 문자열 payload와 UTF-8 계약 구현 계획

> 구현자는 이 계획을 task 순서대로 실행하고 각 checkbox를 갱신합니다. 동작
> 변경은 실패하는 테스트를 먼저 추가하고, 각 task가 독립적으로 검토 가능한
> 상태에서 커밋합니다.

**목표:** 모든 문자열 진입점에 동일한 UTF-8 계약을 적용하고, 구조화 payload의
동적 값을 percent encoding하여 예약문자가 포함된 이름과 채팅도 손실 없이
왕복시킵니다.

**아키텍처:** `libs/protocol`에 text validation과 구조화 payload codec을 두고
서버, Qt 클라이언트, 콘솔 클라이언트와 부하 도구가 이 구현을 공유합니다.
`PacketCodec`은 기존처럼 frame만 담당하며, 서버는 검증을 통과한 raw 요청만
도메인 계층에 전달하고 모든 구조화 응답을 builder로 생성합니다. wire 표현은
구형 구현과 혼용할 수 있으므로 server와 client 변경을 하나의 배포 단위로
완료합니다.

**기술 스택:** C++20, CMake 3.20+, GoogleTest, Qt 6 Test, Linux
`epoll`/TCP integration test

**설계 문서:** `docs/design/2026-09-03-string-payload-utf8.md`

## 전역 제약

- 4바이트 packet header, packet type 번호와 `POSITION_UPDATE` binary 형식은
  변경하지 않습니다.
- `LOGIN_REQ`, `CREATE_ROOM_REQ`, `CHAT_REQ`는 percent encoding하지 않은 raw
  UTF-8을 사용합니다.
- 구조화 payload는 선택적인 첫 `OK` segment와 `key=value` field로 구성하고,
  field separator는 `|`입니다.
- value encoder는 `%`, `|`, `=`만 각각 `%25`, `%7C`, `%3D`로 바꾸며 대문자
  hex를 출력합니다. decoder는 대소문자 hex를 모두 허용합니다.
- UTF-8은 RFC 3629 scalar value만 허용하고 ASCII control `U+0000`~`U+001F`,
  `U+007F`은 거부합니다. Unicode normalization은 수행하지 않습니다.
- 사용자 이름과 방 이름은 ASCII 공백만 양끝에서 제거한 뒤 1~32 raw byte를
  허용합니다. 로그인 lookup key에는 ASCII `A`~`Z` case-fold만 적용합니다.
- 채팅은 공백과 빈 문자열을 보존하고 0~1291 raw byte를 허용합니다.
- 잘못된 요청은 `ERROR`를 보낸 뒤 연결을 유지하며 사용자, 방과 membership을
  변경하거나 broadcast를 만들지 않습니다.
- `libs/protocol`은 다른 프로젝트 library에 의존하지 않고,
  `libs/server-core`는 `protocol`에만 의존하며 Qt client는 Linux 전용 target에
  의존하지 않습니다.
- public include는 `rss/...` 표기를 유지하고 생성 파일 `ui_MainWindow.h`는
  커밋하지 않습니다.
- 프로토콜 변경과 같은 커밋 집합에서 server, 모든 client, 테스트와
  `docs/protocol.md`를 함께 갱신합니다.

## 파일 구조

### 새 protocol 파일

- `libs/protocol/include/rss/protocol/ProtocolError.h`: frame과 text codec이
  공유하는 protocol 예외
- `libs/protocol/src/ProtocolError.cpp`: `ProtocolError` constructor 구현
- `libs/protocol/include/rss/protocol/TextValidation.h`: UTF-8/control 검증과
  ASCII trim API
- `libs/protocol/src/TextValidation.cpp`: RFC 3629 byte-state 검증
- `libs/protocol/include/rss/protocol/StructuredPayload.h`: value encode/decode,
  parsed field collection과 builder API
- `libs/protocol/src/StructuredPayload.cpp`: grammar, duplicate key와 percent escape
  처리
- `tests/protocol/TextValidationTest.cpp`: UTF-8 경계와 ASCII trim 단위 테스트
- `tests/protocol/StructuredPayloadTest.cpp`: value와 payload 왕복 및 오류 단위
  테스트

### 수정할 구현 파일

- `libs/protocol/include/rss/protocol/PacketCodec.h`: `ProtocolError.h` include로
  예외 선언 분리
- `libs/protocol/include/rss/protocol/Packet.h`: 이름/방/채팅 raw byte 상한
- `libs/protocol/src/PacketCodec.cpp`: 분리된 예외 구현 제거
- `libs/protocol/CMakeLists.txt`, `tests/protocol/CMakeLists.txt`: 새 source와 test
- `libs/server-core/src/service/MessageRouter.cpp`: raw 요청 검증과 builder 기반
  응답/broadcast
- `libs/server-core/src/service/RoomService.cpp`: 방 이름 방어 검증, default와
  truncation 제거
- `apps/qt-client/application/include/rss/qt_client/application/ClientController.h`
- `apps/qt-client/application/src/ClientController.cpp`: 송신 검증과 parser 기반
  수신 처리
- `apps/console-client/src/main.cpp`: 송신 검증과 decoded 구조화 payload 출력
- `apps/load-scenario-runner/src/ScenarioClient.cpp`: create response parser
- `apps/load-scenario-runner/src/ScenarioRunner.cpp`: CHAT broadcast parser

### 수정할 테스트와 문서

- `tests/server-core/MessageRouterTest.cpp`
- `tests/server-core/RoomServiceTest.cpp`
- `tests/qt-client/ClientControllerTest.cpp`
- `tests/qt-client/MainWindowTest.cpp`
- `tests/server-net-linux/ScenarioClientTest.cpp`
- `tests/server-net-linux/ScenarioRunnerTest.cpp`
- `docs/protocol.md`, `README.md`, `docs/known-issues.md`,
  `docs/project-status.md`

---

### Task 1: 공용 text validation과 구조화 payload codec

**Files:**

- Create: `libs/protocol/include/rss/protocol/ProtocolError.h`
- Create: `libs/protocol/src/ProtocolError.cpp`
- Create: `libs/protocol/include/rss/protocol/TextValidation.h`
- Create: `libs/protocol/src/TextValidation.cpp`
- Create: `libs/protocol/include/rss/protocol/StructuredPayload.h`
- Create: `libs/protocol/src/StructuredPayload.cpp`
- Create: `tests/protocol/TextValidationTest.cpp`
- Create: `tests/protocol/StructuredPayloadTest.cpp`
- Modify: `libs/protocol/include/rss/protocol/PacketCodec.h`
- Modify: `libs/protocol/include/rss/protocol/Packet.h`
- Modify: `libs/protocol/src/PacketCodec.cpp`
- Modify: `libs/protocol/CMakeLists.txt`
- Modify: `tests/protocol/CMakeLists.txt`

**Interfaces:**

- Produces:
  `bool rss::protocol::isValidText(std::string_view text) noexcept`
- Produces:
  `std::string_view rss::protocol::trimAsciiWhitespace(std::string_view text) noexcept`
- Produces:
  `std::string rss::protocol::encodeStructuredValue(std::string_view value)`
- Produces:
  `std::string rss::protocol::decodeStructuredValue(std::string_view value)`
- Produces:
  `StructuredPayload StructuredPayload::parse(std::string_view payload)`
- Produces:
  `std::optional<std::string_view> StructuredPayload::status() const noexcept`
- Produces:
  `std::optional<std::string_view> StructuredPayload::field(std::string_view key) const noexcept`
- Produces:
  `std::string_view StructuredPayload::requireField(std::string_view key) const`
- Produces:
  `const std::vector<StructuredField>& StructuredPayload::fields() const noexcept`
- Produces: `StructuredPayloadBuilder()`와
  `explicit StructuredPayloadBuilder(std::string_view status)`
- Produces:
  `StructuredPayloadBuilder& StructuredPayloadBuilder::addField(std::string_view key, std::string_view value)`
- Produces: `std::string StructuredPayloadBuilder::build() const`

- [ ] **Step 1: UTF-8 validator와 ASCII trim 실패 테스트 작성**

`TextValidationTest.cpp`에 아래 경계를 명시적으로 고정합니다.

```cpp
TEST(TextValidationTest, AcceptsAsciiAndRfc3629BoundaryScalars) {
  EXPECT_TRUE(isValidText("hello 한글"));
  EXPECT_TRUE(isValidText("\xC2\x80"));
  EXPECT_TRUE(isValidText("\xF4\x8F\xBF\xBF"));
}

TEST(TextValidationTest, RejectsMalformedUtf8AndAsciiControls) {
  for (const std::string_view text : {
           "\xC2", "\xE2\x28\xA1", "\xC0\xAF", "\xED\xA0\x80",
           "\xF4\x90\x80\x80", "line\nfeed", "\x7F"}) {
    EXPECT_FALSE(isValidText(text)) << std::string(text);
  }
}

TEST(TextValidationTest, TrimsOnlyAsciiWhitespaceAtEdges) {
  EXPECT_EQ(trimAsciiWhitespace(" \t한글\r\n"), "한글");
  EXPECT_EQ(trimAsciiWhitespace("\xC2\xA0name\xC2\xA0"),
            "\xC2\xA0name\xC2\xA0");
}
```

- [ ] **Step 2: 새 protocol test source를 target에 등록하고 RED 확인**

`tests/protocol/CMakeLists.txt`의 executable source에 두 test 파일을 추가한 뒤
실행합니다.

```bash
cmake --preset core-dev
cmake --build --preset core-dev --target rss_protocol_tests --parallel
```

Expected: `TextValidation.h`와 함수 정의가 없어 compile 실패합니다.

- [ ] **Step 3: RFC 3629 validator와 ASCII trim 최소 구현 작성**

`TextValidation.h`에는 위 두 함수를 선언합니다. `TextValidation.cpp`는 lead
byte별로 길이와 continuation 범위를 확인합니다.

```cpp
if (lead <= 0x7fU) {
  if (lead <= 0x1fU || lead == 0x7fU) {
    return false;
  }
  ++index;
  continue;
}
if (lead >= 0xc2U && lead <= 0xdfU) {
  // 정확히 한 continuation byte를 요구합니다.
} else if (lead == 0xe0U) {
  // 두 번째 byte는 0xa0..0xbf로 제한해 overlong을 막습니다.
} else if (lead >= 0xe1U && lead <= 0xecU) {
  // 일반 3-byte scalar를 검사합니다.
} else if (lead == 0xedU) {
  // 두 번째 byte를 0x80..0x9f로 제한해 surrogate를 막습니다.
} else if (lead >= 0xeeU && lead <= 0xefU) {
  // 일반 3-byte scalar를 검사합니다.
} else if (lead == 0xf0U) {
  // 두 번째 byte는 0x90..0xbf로 제한합니다.
} else if (lead >= 0xf1U && lead <= 0xf3U) {
  // 일반 4-byte scalar를 검사합니다.
} else if (lead == 0xf4U) {
  // 두 번째 byte를 0x80..0x8f로 제한합니다.
} else {
  return false;
}
```

ASCII trim 대상은 `' '`, `\t`, `\n`, `\r`, `\f`, `\v` 여섯 byte로 한정합니다.

- [ ] **Step 4: text validation GREEN 확인**

```bash
cmake --build --preset core-dev --target rss_protocol_tests --parallel
./build/core-dev/tests/protocol/rss_protocol_tests \
  --gtest_filter='TextValidationTest.*'
```

Expected: 모든 `TextValidationTest`가 PASS합니다.

- [ ] **Step 5: value codec과 구조화 grammar 실패 테스트 작성**

`StructuredPayloadTest.cpp`에 canonical encoding, decoding, field lookup과 오류를
추가합니다.

```cpp
TEST(StructuredPayloadTest, EncodesReservedBytesAndRoundTripsUtf8) {
  const std::string original = "한글%|=";
  EXPECT_EQ(encodeStructuredValue(original), "한글%25%7C%3D");
  EXPECT_EQ(decodeStructuredValue("한글%25%7c%3d"), original);
}

TEST(StructuredPayloadTest, BuildsAndParsesStatusAndFields) {
  const auto wire = StructuredPayloadBuilder("OK")
                        .addField("event", "CHAT")
                        .addField("name", "kim|role=admin%")
                        .addField("message", "a=b|c")
                        .build();
  EXPECT_EQ(wire,
            "OK|event=CHAT|name=kim%7Crole%3Dadmin%25|message=a%3Db%7Cc");
  const auto parsed = StructuredPayload::parse(wire);
  EXPECT_EQ(parsed.status(), "OK");
  EXPECT_EQ(parsed.requireField("name"), "kim|role=admin%");
  EXPECT_EQ(parsed.requireField("message"), "a=b|c");
}

TEST(StructuredPayloadTest, RejectsMalformedGrammarAndEscapes) {
  for (const std::string_view payload : {
           "", "event", "=value", "event=ONE|event=TWO", "event=CHAT|",
           "event=%", "event=%2", "event=%GG", "NO|event=CHAT"}) {
    EXPECT_THROW(StructuredPayload::parse(payload), ProtocolError);
  }
}

TEST(StructuredPayloadTest, RejectsInvalidDecodedTextAndMissingField) {
  EXPECT_THROW(StructuredPayload::parse("name=%C0%AF"), ProtocolError);
  const auto parsed = StructuredPayload::parse("event=JOIN");
  EXPECT_THROW(parsed.requireField("room_id"), ProtocolError);
}
```

Builder test에는 잘못된 key, duplicate key, `OK` 이외 status와 invalid UTF-8
value가 `ProtocolError`를 던지는 사례도 포함합니다.

- [ ] **Step 6: 구조화 payload codec RED 확인**

```bash
cmake --build --preset core-dev --target rss_protocol_tests --parallel
```

Expected: `StructuredPayload.h` 또는 선언된 class와 함수가 없어 compile
실패합니다.

- [ ] **Step 7: 예외 분리와 구조화 payload codec 최소 구현 작성**

`ProtocolError`를 별도 header/source로 옮기고 `PacketCodec.h`가 새 header를
include하게 합니다. `StructuredPayload.h`의 public 형태는 다음으로 고정합니다.

```cpp
using StructuredField = std::pair<std::string, std::string>;

class StructuredPayload final {
 public:
  static StructuredPayload parse(std::string_view payload);
  [[nodiscard]] std::optional<std::string_view> status() const noexcept;
  [[nodiscard]] std::optional<std::string_view> field(
      std::string_view key) const noexcept;
  [[nodiscard]] std::string_view requireField(std::string_view key) const;
  [[nodiscard]] const std::vector<StructuredField>& fields() const noexcept;

 private:
  std::optional<std::string> status_;
  std::vector<StructuredField> fields_;
};

class StructuredPayloadBuilder final {
 public:
  StructuredPayloadBuilder() = default;
  explicit StructuredPayloadBuilder(std::string_view status);
  StructuredPayloadBuilder& addField(std::string_view key,
                                     std::string_view value);
  [[nodiscard]] std::string build() const;

 private:
  std::optional<std::string> status_;
  std::vector<StructuredField> fields_;
};
```

Parser는 첫 segment가 정확히 `OK`일 때만 status로 인정하고, 나머지는 모두
`key=value`여야 합니다. 첫 `=`만 key/value 경계로 사용하되 literal `=`가
value에 나타나면 non-canonical input으로 거부합니다. key는 ASCII 영숫자와
underscore만 허용하고 vector 선형 탐색으로 duplicate를 거부합니다. decode 뒤
`isValidText()`를 호출합니다. `libs/protocol/CMakeLists.txt`에는
`ProtocolError.cpp`, `TextValidation.cpp`, `StructuredPayload.cpp`를 target source로
등록합니다.

- [ ] **Step 8: 고정 byte 상한을 protocol 상수로 반영**

`Packet.h`에 다음 상수를 두고 기존 3939 계산을 교체합니다.

```cpp
constexpr std::size_t kMaxUserNameBytes = 32;
constexpr std::size_t kMaxRoomNameBytes = 32;
constexpr std::size_t kMaxChatMessageBytes = 1291;
```

각 상수는 raw UTF-8, trim 이후, percent encoding 이전 byte 수라는 주석을
붙입니다. packet 크기 상수의 기존 `uint16_t` type은 유지합니다.

- [ ] **Step 9: protocol 전체 GREEN과 회귀 확인**

```bash
cmake --build --preset core-dev --target rss_protocol_tests --parallel
./build/core-dev/tests/protocol/rss_protocol_tests
```

Expected: 새 codec test와 기존 `PacketCodecTest`가 모두 PASS합니다.

- [ ] **Step 10: protocol 변경 커밋**

```bash
git add libs/protocol tests/protocol
git commit -m "기능: 구조화 문자열 payload codec 추가"
```

---

### Task 2: 서버 요청 검증과 안전한 응답 생성

**Files:**

- Modify: `libs/server-core/src/service/MessageRouter.cpp`
- Modify: `libs/server-core/src/service/RoomService.cpp`
- Modify: `tests/server-core/MessageRouterTest.cpp`
- Modify: `tests/server-core/RoomServiceTest.cpp`

**Interfaces:**

- Consumes: Task 1의 `isValidText`, `trimAsciiWhitespace`, 이름/채팅 상한,
  `StructuredPayloadBuilder`, `StructuredPayload::parse`
- Preserves: `RoomService::createRoom(std::uint64_t, std::string)` signature
- Produces: 모든 `LoginRes`, room response와 `RoomBroadcast`의 canonical encoded
  wire payload

- [ ] **Step 1: 잘못된 이름이 상태를 바꾸지 않는 실패 테스트 작성**

`RoomServiceTest.cpp`에 empty, ASCII-trimmed empty, 33-byte, invalid UTF-8과
control byte 방 이름이 모두 거부되고 다음 정상 방의 ID가 1인 사례를 추가합니다.

```cpp
TEST(RoomServiceTest, RejectsInvalidRoomNamesWithoutCreatingRoom) {
  RoomService service;
  ASSERT_TRUE(attach(service, 100, 1, "alice").ok);

  for (const std::string name : {
           std::string{}, std::string{" \t"}, std::string(33, 'x'),
           std::string("\xC0\xAF", 2), std::string{"bad\nname"}}) {
    const auto rejected = service.createRoom(100, name);
    EXPECT_FALSE(rejected.ok);
    EXPECT_EQ(rejected.error, "invalid room name");
  }

  const auto created = service.createRoom(100, " arena ");
  ASSERT_TRUE(created.ok);
  EXPECT_EQ(created.room_id, 1U);
}
```

`MessageRouterTest.cpp`에는
`RejectsInvalidLoginWithoutMutatingSession`과
`RejectsInvalidRoomNameWithoutCreatingRoom`을 추가합니다. 첫 test는 invalid
UTF-8 로그인 뒤 같은 session이 `alice`로 로그인할 수 있음을 확인하고, 두 번째
test는 invalid 방 이름 뒤 정상 방의 ID가 1임을 확인합니다.

- [ ] **Step 2: 서버 검증 RED 확인**

```bash
cmake --build --preset core-dev --target rss_server_core_tests --parallel
./build/core-dev/tests/server-core/rss_server_core_tests \
  --gtest_filter='RoomServiceTest.RejectsInvalidRoomNamesWithoutCreatingRoom:MessageRouterTest.RejectsInvalidLoginWithoutMutatingSession:MessageRouterTest.RejectsInvalidRoomNameWithoutCreatingRoom'
```

Expected: 빈 방이 `room`으로 생성되거나 긴 방 이름이 잘려 저장되어 FAIL합니다.

- [ ] **Step 3: 이름 검증과 normalization 구현**

`MessageRouter.cpp`의 지역 `isAsciiSpace`를 제거하고 login normalization을 다음
순서로 바꿉니다.

```cpp
const auto trimmed = protocol::trimAsciiWhitespace(input);
if (trimmed.empty() || trimmed.size() > protocol::kMaxUserNameBytes ||
    !protocol::isValidText(trimmed)) {
  return std::nullopt;
}
```

`MessageRouter`의 `CreateRoomReq` 분기는 `trimAsciiWhitespace` 후 1~32 byte와
text validity를 확인하고, 실패하면 `invalid room name`을 emit한 뒤
`RoomService`를 호출하지 않습니다. `RoomService.cpp`의 `kDefaultRoomName`과
truncation helper를 제거합니다. `RoomService::createRoom`도 lock을 얻기 전에
같은 검증을 방어적으로 수행하고 실패 시
`RoomActionResult{.ok=false, .error="invalid room name"}` 상태로 반환합니다.
유효한 trimmed 문자열만 lock 안에서 `Lobby::createRoom`에 넘깁니다.

- [ ] **Step 4: 이름 검증 GREEN 확인**

```bash
cmake --build --preset core-dev --target rss_server_core_tests --parallel
./build/core-dev/tests/server-core/rss_server_core_tests \
  --gtest_filter='RoomServiceTest.*RoomName*:MessageRouterTest.*LoginName*:MessageRouterTest.*Invalid*'
```

Expected: 이름 검증 test가 PASS하고 기존 login ASCII trim/case-fold test도
PASS합니다.

- [ ] **Step 5: 구조화 응답 round-trip과 chat 상한 실패 테스트 작성**

`MessageRouterTest.cpp`에 `EncodesStructuredDynamicValues`를 추가합니다. 이름
`kim|role=admin%`으로 로그인한 뒤 login response, create/join response와
broadcast를 `StructuredPayload::parse`하여 원래 name을 얻습니다.
`PreservesWorstCaseMaximumChatWithinPacketLimit`은 다음 최악 조건을 고정합니다.

```cpp
const std::string reserved_name(32, '|');
const std::string maximum_message(protocol::kMaxChatMessageBytes, '=');
// reserved_name으로 로그인하고 방을 만든 뒤 maximum_message를 전송합니다.
const auto packet = decodeSingleMessage(chat.front());
EXPECT_LE(packet.payload.size() + protocol::kPacketHeaderSize,
          protocol::kMaxPacketSize);
const auto parsed = StructuredPayload::parse(payloadToString(packet));
EXPECT_EQ(parsed.requireField("name"), reserved_name);
EXPECT_EQ(parsed.requireField("message"), maximum_message);
```

`RejectsInvalidChatWithoutBroadcasting`에서는 1292-byte 요청은
`chat message too large`, invalid UTF-8/control 요청은 `invalid chat message`를
반환하며 추가 broadcast가 없음을 확인합니다.
`PreservesEmptyAndWhitespaceChat`에서는 빈 문자열과 공백만 있는 chat이 성공하고
parse 결과가 입력과 같음을 확인합니다.

- [ ] **Step 6: server builder/채팅 검증 RED 확인**

```bash
cmake --build --preset core-dev --target rss_server_core_tests --parallel
./build/core-dev/tests/server-core/rss_server_core_tests \
  --gtest_filter='MessageRouterTest.EncodesStructuredDynamicValues:MessageRouterTest.PreservesWorstCaseMaximumChatWithinPacketLimit:MessageRouterTest.RejectsInvalidChatWithoutBroadcasting:MessageRouterTest.PreservesEmptyAndWhitespaceChat'
```

Expected: 예약문자가 field separator로 노출되거나 기존 상한/공백 처리 때문에
FAIL합니다.

- [ ] **Step 7: 모든 구조화 응답과 broadcast를 builder로 생성**

`userPrefix`와 `okPayload`의 문자열 결합을 제거하고 다음 helper 형태로
대체합니다.

```cpp
void addUserFields(protocol::StructuredPayloadBuilder& builder,
                   const domain::User& user) {
  builder.addField("user_id", user.id.toString())
      .addField("session_id", std::to_string(user.session_id))
      .addField("name", user.name);
}

std::string roomPayload(std::optional<std::string_view> status,
                        std::string_view event,
                        const RoomActionResult& result) {
  auto builder = status.has_value()
                     ? protocol::StructuredPayloadBuilder(*status)
                     : protocol::StructuredPayloadBuilder();
  builder.addField("event", event);
  if (result.room_id != 0) {
    builder.addField("room_id", std::to_string(result.room_id));
  }
  addUserFields(builder, result.actor);
  return builder.build();
}
```

Login response는 `OK` builder와 user fields를 사용합니다. Create/Join/Leave
response와 기존 JOIN/LEAVE/disconnect broadcast는 `OK` status를 유지합니다.
기존 CHAT/POSITION broadcast는 status 없이 `event`부터 시작합니다. 이 구분은
예약문자가 없는 기존 wire byte를 바꾸지 않기 위한 것입니다. CHAT의 `message`,
POSITION의 `x`, `y`도 각각 `addField`로 추가합니다.

- [ ] **Step 8: chat raw 입력을 상태 변경 전에 검증**

`ChatReq` 분기 시작에서 raw payload를 한 번 소유 문자열로 읽고 아래 순서를
지킵니다.

```cpp
const auto message = payloadText(packet);
if (message.size() > protocol::kMaxChatMessageBytes) {
  sink.emit(error(session_id, "chat message too large"));
  return;
}
if (!protocol::isValidText(message)) {
  sink.emit(error(session_id, "invalid chat message"));
  return;
}
const auto result = room_service_.chat(session_id);
```

빈 message도 `isValidText`가 true이므로 그대로 broadcast합니다. builder가 만든
최종 payload는 기존 `PacketCodec::encode`의 4096-byte 검사를 한 번 더
통과합니다.

- [ ] **Step 9: server-core 전체 GREEN 확인**

```bash
cmake --build --preset core-dev --target rss_server_core_tests --parallel
./build/core-dev/tests/server-core/rss_server_core_tests
```

Expected: server-core test 전체가 PASS하고 기존 예약문자 없는 payload 기대값은
status 제거가 반영된 broadcast를 제외하면 그대로입니다.

- [ ] **Step 10: 서버 변경 커밋**

```bash
git add libs/server-core tests/server-core
git commit -m "기능: 서버 문자열 입력 검증과 안전한 응답 생성"
```

---

### Task 3: Qt 클라이언트 송신 검증과 원자적 응답 parsing

**Files:**

- Modify: `apps/qt-client/application/include/rss/qt_client/application/ClientController.h`
- Modify: `apps/qt-client/application/src/ClientController.cpp`
- Modify: `tests/qt-client/ClientControllerTest.cpp`
- Modify: `tests/qt-client/MainWindowTest.cpp`

**Interfaces:**

- Consumes: Task 1의 validator, trim, 상한과 `StructuredPayload::parse`
- Changes:
  `bool ClientController::sendTextPacket(PacketType, std::string_view)` private
  helper
- Preserves: public Qt slot/method signature와 `ChatLogEntry` model
- Produces: malformed response에서 state/session id를 유지하고
  `LogKind::Error` entry를 하나 추가하는 동작
- Produces: anonymous namespace의
  `std::uint64_t positiveInteger(std::string_view text)` parser; 빈 값, 0,
  overflow와 일부 문자만 소비한 값은 `ProtocolError`로 거부

- [ ] **Step 1: Qt 송신 byte 계약 실패 테스트 작성**

`ClientControllerTest.cpp`에서 login과 room은 ASCII edge만 trim하고, 33 raw
byte/invalid control을 보내지 않는지 확인합니다. chat test는 기존 blank 거부를
다음 보존 계약으로 교체합니다.

```cpp
void preservesChatWhitespaceAndAllowsEmptyMessage() {
  FakeSessionTransport transport;
  ClientController controller(transport);
  enterRoom(controller, transport);

  QVERIFY(controller.sendChat("  hello  "));
  QCOMPARE(transport.lastPayload(), std::string("  hello  "));
  QVERIFY(controller.sendChat(""));
  QCOMPARE(transport.lastPayload(), std::string());
}
```

1291-byte chat은 전송되고 1292-byte chat은 `validationFailed`와 함께 거부되는
test를 추가합니다. 한글은 UTF-8 byte 길이를 사용하므로 `가` 10개(30 byte)는
허용하고 11개(33 byte)는 이름으로 거부합니다.

- [ ] **Step 2: Qt 송신 RED 확인**

```bash
cmake --preset qt-client-dev
cmake --build --preset qt-client-dev --target \
  rss_qt_client_controller_tests --parallel
QT_QPA_PLATFORM=offscreen \
  ./build/qt-client-dev/tests/qt-client/rss_qt_client_controller_tests
```

Expected: 현재 `QString::trimmed()`가 chat whitespace/empty 계약을 위반하여
FAIL합니다.

- [ ] **Step 3: Qt 요청을 UTF-8 byte 기준으로 검증하고 raw 전송**

각 입력을 `QByteArray utf8 = value.toUtf8()`로 만든 뒤 `std::string_view`를
구성합니다. login/room은 `trimAsciiWhitespace` 결과를 검증하고 전송하며 chat은
trim하지 않습니다. private helper를 아래 signature로 바꿉니다.

```cpp
bool sendTextPacket(protocol::PacketType type, std::string_view payload);
```

login 오류는 `Enter a valid user name of at most 32 UTF-8 bytes.`, room 오류는
`Enter a valid room name of at most 32 UTF-8 bytes.`, chat control/UTF-8 오류는
`Enter a valid chat message.`, 초과 오류는
`Chat messages are limited to 1291 UTF-8 bytes.`를 emit합니다. transport 호출은
동기 복사 계약이므로 지역 `QByteArray`가 살아 있는 동안 view를 전달합니다.

- [ ] **Step 4: Qt 송신 GREEN 확인**

```bash
cmake --build --preset qt-client-dev --target \
  rss_qt_client_controller_tests --parallel
QT_QPA_PLATFORM=offscreen \
  ./build/qt-client-dev/tests/qt-client/rss_qt_client_controller_tests
```

Expected: 송신 validation test가 PASS합니다.

- [ ] **Step 5: escaped 수신과 malformed 상태 불변 실패 테스트 작성**

기존 literal `message=hello|there` fixture를 canonical
`message=hello%7Cthere`로 바꾸고 아래 사례를 추가합니다.

```cpp
void decodesEscapedChatAuthorAndMessage() {
  // LoginRes의 session_id=10으로 로그인 상태를 만듭니다.
  transport.receive(packet(
      PacketType::RoomBroadcast,
      "event=CHAT|room_id=1|user_id=00000000-0000-0000-0000-000000000001|"
      "session_id=10|name=kim%7Cadmin%3Dyes%25|message=hello%7Cthere"));
  const auto entry = log_spy.at(0).at(0).value<ChatLogEntry>();
  QCOMPARE(entry.author, QString::fromUtf8("kim|admin=yes%"));
  QCOMPARE(entry.text, QString::fromUtf8("hello|there"));
  QVERIFY(entry.is_own);
}

void malformedLoginResponseDoesNotChangeState() {
  transport.receive(packet(PacketType::LoginRes,
                           "OK|user_id=id|session_id=%GG|name=alice"));
  QCOMPARE(controller.state(), ClientState::Connected);
  QCOMPARE(log_spy.count(), 1);
  QCOMPARE(log_spy.at(0).at(0).value<ChatLogEntry>().kind, LogKind::Error);
}
```

LoginRes에는 `user_id`, `session_id`, `name`, Create/Join/Leave response에는
`event`, `room_id`, `user_id`, `session_id`, `name`을 required field로 검사합니다.
response event는 packet type별로 `CREATE_ROOM`, `JOIN_ROOM`, `LEAVE_ROOM`과 정확히
일치해야 합니다. CHAT broadcast에는 `event`, `room_id`, `user_id`, `session_id`,
`name`, `message`를 요구하고 JOIN/LEAVE/POSITION에는 `message`를 요구하지
않습니다. JOIN/LEAVE broadcast의 status는 `OK`, CHAT/POSITION status는 없어야
합니다. 숫자 field는 전체 문자열이 유효한 10진수이고 room/session id가 0보다
커야 하며 `user_id`와 `name`은 비어 있으면 안 됩니다. duplicate/missing field,
invalid escape, invalid UTF-8은 모두 state를 변경하지 않고
`Protocol error: invalid structured payload.` error log를 하나 남깁니다.

- [ ] **Step 6: Qt 수신 RED 확인**

```bash
cmake --build --preset qt-client-dev --target \
  rss_qt_client_controller_tests rss_qt_main_window_tests --parallel
ctest --preset qt-client-dev -R 'rss_qt_(client_controller|main_window)_tests' \
  --output-on-failure
```

Expected: 현재 delimiter 검색이 escape를 decode하지 않고 malformed response도
state를 변경하여 FAIL합니다.

- [ ] **Step 7: parser 결과를 모두 검증한 뒤 Qt state를 한 번만 변경**

`payloadText`, `isSuccessfulResponse`, `fieldValue`, `sessionId`, 기존
`chatEntry(QString, ...)`를 제거합니다. 구조화 packet 처리 helper는 먼저 raw
`std::string`을 parse하고 required schema와 numeric field를 지역 변수에 모두
검증합니다. 성공 후에만 `session_id_`, `state_`와 log signal을 갱신합니다.
`positiveInteger`는 `std::from_chars`가 입력 전체를 소비했는지 확인하고 0 또는
overflow를 `ProtocolError`로 바꿉니다.

```cpp
try {
  const auto parsed = protocol::StructuredPayload::parse(raw_payload);
  if (parsed.status() != std::optional<std::string_view>{"OK"}) {
    throw protocol::ProtocolError("response status is not OK");
  }
  const auto parsed_session_id = positiveInteger(parsed.requireField("session_id"));
  static_cast<void>(parsed.requireField("user_id"));
  static_cast<void>(parsed.requireField("name"));
  session_id_ = parsed_session_id;
  setState(ClientState::LoggedIn);
} catch (const protocol::ProtocolError&) {
  emit logEntryAdded(logEntry(LogKind::Error,
                              "Protocol error: invalid structured payload."));
  return;
}
```

CHAT log entry는 decoded `name`과 `message` field를 `QString::fromUtf8(data,
size)`로 변환합니다. JOIN/LEAVE/POSITION 같은 system broadcast는 parsed field
순서를 순회해 `key=decoded-value`를 `|`로 이어 사람이 읽을 수 있게 표시합니다.
`Error`와 `Pong` 같은 비구조화 text도 `isValidText`를 통과한 뒤에만 변환합니다.

- [ ] **Step 8: MainWindow fixture와 전체 Qt test GREEN 확인**

`MainWindowTest.cpp`의 축약 `OK` fixture를 required field가 포함된 canonical
response로 교체하고 broadcast fixture의 동적 값을 encoding합니다.

```bash
cmake --build --preset qt-client-dev --parallel
ctest --preset qt-client-dev --output-on-failure
```

Expected: Qt client와 widget test 전체가 PASS합니다.

- [ ] **Step 9: Qt 변경 커밋**

```bash
git add apps/qt-client tests/qt-client
git commit -m "기능: Qt 문자열 payload 검증과 parsing 적용"
```

---

### Task 4: Linux 콘솔·부하 도구와 실제 서버 왕복

**Files:**

- Modify: `apps/console-client/src/main.cpp`
- Modify: `apps/load-scenario-runner/src/ScenarioClient.cpp`
- Modify: `apps/load-scenario-runner/src/ScenarioRunner.cpp`
- Modify: `tests/server-net-linux/ScenarioClientTest.cpp`
- Modify: `tests/server-net-linux/ScenarioRunnerTest.cpp`

**Interfaces:**

- Consumes: Task 1의 validator, 상한과 `StructuredPayload::parse`
- Preserves: `ScenarioClient` public API
- Produces: 콘솔의 decoded 표시와 scenario runner의 field-order 독립 CHAT parsing

- [ ] **Step 1: scenario parser와 실제 왕복 실패 테스트 작성**

`ScenarioClientTest.cpp`에 raw loopback peer가 다음 response를 보내면 createRoom이
42를 반환하는 test를 추가합니다.

```cpp
const auto response = PacketCodec::encode(
    PacketType::CreateRoomRes,
    "OK|event=CREATE_ROOM|name=a%7Cb|room_id=42|"
    "user_id=00000000-0000-0000-0000-000000000001|session_id=1");
```

`room_id` duplicate, missing, non-numeric과 malformed escape는 `runtime_error`로
측정 실패해야 합니다. 실제 server test에는 이름 `kim|role=admin%`, 방
`room|tier=1%`, 채팅 `hello|kind=admin%`을 보내고 response/broadcast를 공용
parser로 읽어 세 원문이 복원되는 사례를 추가합니다. 기존 3000-byte chat
fixture는 새 상한 안인 1000 byte로 줄입니다.

- [ ] **Step 2: Linux 도구 test RED 확인**

Linux host에서 실행합니다.

```bash
cmake --preset linux-dev
cmake --build --preset linux-dev --target rss_server_net_tests --parallel
./build/linux-dev/tests/server-net-linux/rss_server_net_tests \
  --gtest_filter='ScenarioClientTest.*Structured*:ScenarioClientTest.*Escaped*:ScenarioRunnerTest.*Broadcast*'
```

Expected: 현재 `find("room_id=")`와 terminal-message parsing이 malformed payload나
field 순서 변경을 올바르게 처리하지 못해 FAIL합니다.

- [ ] **Step 3: ScenarioClient create response를 공용 parser로 전환**

`createRoom`은 response text를 parse하고 status `OK`와 required `room_id`를
확인한 뒤 기존 `from_chars` 전체 소비 검사를 적용합니다.

```cpp
const auto parsed = rss::protocol::StructuredPayload::parse(
    rss::protocol::payloadToString(response));
if (parsed.status() != std::optional<std::string_view>{"OK"}) {
  throw std::runtime_error("create room response is not successful");
}
const auto room_id_text = parsed.requireField("room_id");
```

`ProtocolError`는 `create room response is malformed` 문구의 `runtime_error`로
변환하여 scenario 실패 원인을 안정적으로 보고합니다.

- [ ] **Step 4: ScenarioRunner CHAT 수신을 공용 parser로 전환**

`messagePayload` delimiter helper를 제거합니다. receiver loop는 payload를
parse하고 `event == "CHAT"`일 때만 decoded `message`를 기존 `parsePayload`에
넘깁니다.

```cpp
const auto parsed = rss::protocol::StructuredPayload::parse(broadcast);
if (parsed.requireField("event") != "CHAT") {
  continue;
}
const auto identity = parsePayload(parsed.requireField("message"));
```

`ProtocolError`와 기존 `invalid_argument`를 unexpected packet 한 건으로
집계합니다. field 순서나 encoded separator는 측정 identity를 바꾸지 않습니다.

- [ ] **Step 5: 콘솔 송신 검증과 decoded 출력 구현**

`makePacket`에서 `/login`, `/create`, `/chat`과 일반 chat을 server와 같은 raw
byte 규칙으로 검증합니다. login/create는 ASCII trim 후 전송하고 chat은 원문을
보존합니다. 오류 문구는 server 문구와 동일한 `invalid user name`,
`invalid room name`, `invalid chat message`, `chat message too large`를 사용합니다.

`readLoop`은 Login/방 response와 RoomBroadcast type만
`StructuredPayload::parse`하고, status와 `fields()`의 decoded value를 사람이
읽는 문자열로 출력합니다.

```cpp
void printStructured(const StructuredPayload& payload) {
  bool needs_separator = false;
  if (const auto status = payload.status(); status.has_value()) {
    std::cout << *status;
    needs_separator = true;
  }
  for (const auto& [key, value] : payload.fields()) {
    std::cout << (needs_separator ? "|" : "") << key << '=' << value;
    needs_separator = true;
  }
}
```

malformed structured packet은 connection을 닫지 않고
`[PROTOCOL_ERROR] invalid structured payload`를 출력합니다. `ERROR`, `PONG`은
valid text일 때 raw 출력하고 invalid text이면 같은 protocol error를 출력합니다.

- [ ] **Step 6: Linux 전체 build/test GREEN 확인**

```bash
cmake --build --preset linux-dev --parallel
ctest --preset linux-dev --output-on-failure
```

Expected: 실제 server 왕복을 포함한 Linux test 전체가 PASS합니다.

- [ ] **Step 7: Linux client/tool 변경 커밋**

```bash
git add apps/console-client apps/load-scenario-runner tests/server-net-linux
git commit -m "기능: 콘솔과 부하 도구에 구조화 payload 적용"
```

---

### Task 5: protocol 문서, 상태 문서와 전체 품질 검증

**Files:**

- Modify: `docs/protocol.md`
- Modify: `README.md`
- Modify: `docs/known-issues.md`
- Modify: `docs/project-status.md`
- Review: `docs/design/2026-09-03-string-payload-utf8.md`

**Interfaces:**

- Consumes: Task 1~4에서 확정된 wire grammar, 오류 문구와 상한
- Produces: 구현과 일치하는 공개 protocol 계약과 다음 작업 우선순위

- [ ] **Step 1: protocol 문서의 wire 예시와 오류 계약 갱신**

`docs/protocol.md`에 다음 내용을 한곳에서 찾을 수 있도록 정리합니다.

```text
LOGIN_REQ / CREATE_ROOM_REQ / CHAT_REQ: raw UTF-8
structured value escapes: % -> %25, | -> %7C, = -> %3D
name=kim%7Crole%3Dadmin%25
message=hello%7Cthere
user name: 1..32 raw bytes after ASCII trim
room name: 1..32 raw bytes after ASCII trim
chat message: 0..1291 raw bytes, whitespace preserved
```

grammar, duplicate/missing field, invalid escape, RFC 3629/control 거부,
normalization 미수행, raw/encoded 크기 기준, server 오류 문구와 연결 유지 정책을
기록합니다. CHAT broadcast의 `message`가 마지막이어야 한다는 예외 설명은
삭제하고 모든 예시의 dynamic value를 canonical encoding으로 맞춥니다.

- [ ] **Step 2: 사용자 문서와 상태 문서 갱신**

`README.md`의 console/Qt 사용 설명에 이름·방 32 UTF-8 byte, chat 1291 UTF-8
byte 제한과 예약문자 지원을 짧게 추가합니다. 실행 명령과 build 절차는 바뀌지
않으므로 `CONTRIBUTING.md`는 수정하지 않습니다.

`docs/known-issues.md`에서 문자열 delimiter/UTF-8 항목을 제거합니다.
`docs/project-status.md` 완료 이력에 공용 codec, server/client 적용과 검증 결과를
추가하고 다음 우선순위를 아래 순서로 올립니다.

1. Qt request pending 상태와 중복 요청 방지
2. Qt chat log 및 pending write byte 상한
3. protocol version negotiation

- [ ] **Step 3: 문서와 구현 상수 일치 검사**

```bash
rg -n '3939|message.*last|마지막.*message|kDefaultRoomName|resize\(32\)' \
  README.md docs libs apps tests
rg -n '1291|kMaxChatMessageBytes|kMaxUserNameBytes|kMaxRoomNameBytes' \
  README.md docs libs apps tests
```

Expected: 폐기된 3939 상한, terminal-message 예외, 방 이름 default/truncation이
활성 문서나 구현에 없고 새 상한은 설계·protocol·코드·테스트에 일치합니다.

- [ ] **Step 4: core build와 test 새로 실행**

```bash
cmake --preset core-dev
cmake --build --preset core-dev --parallel
ctest --preset core-dev --output-on-failure
```

Expected: configure/build 성공, core test 100% PASS합니다.

- [ ] **Step 5: Qt build와 test 새로 실행**

```bash
cmake --preset qt-client-dev
cmake --build --preset qt-client-dev --parallel
ctest --preset qt-client-dev --output-on-failure
```

Expected: configure/build 성공, Qt test 100% PASS합니다.

- [ ] **Step 6: Linux build와 test 새로 실행**

Linux host에서 실행합니다.

```bash
cmake --preset linux-dev
cmake --build --preset linux-dev --parallel
ctest --preset linux-dev --output-on-failure
```

Expected: server, console, scenario tool build 성공과 Linux test 100% PASS입니다.

- [ ] **Step 7: formatter, static analysis와 sanitizer 검증**

```bash
cmake --build --preset linux-dev --target format-check
cmake --build --preset linux-dev --target tidy-check
cmake -S . -B build/sanitize \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRSS_BUILD_NETWORK_TARGETS=OFF \
  -DRSS_BUILD_POSTGRES=OFF \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/sanitize --parallel
ctest --test-dir build/sanitize --output-on-failure
```

Expected: format 위반 0건, `tidy-check` 사용 가능 환경에서는 진단 0건,
ASan/UBSan 오류 없이 core test가 PASS합니다. Linux 또는 tidy 도구를 사용할 수
없는 환경이면 명령과 정확한 제약을 PR 검증란에 기록하고 가능한 preset 검증은
모두 완료합니다.

- [ ] **Step 8: diff와 설계 요구사항 대조**

```bash
git diff --check main...HEAD
git diff --stat main...HEAD
git status --short
```

설계 문서의 목표, 문자열 종류, grammar, encoding, UTF-8, 고정 상한, server
처리, client 처리, 호환성과 테스트 전략 각 절을 차례로 대조합니다. unrelated
formatting, 생성 파일, build artifact와 내부 작업 흔적이 diff에 없어야 합니다.

- [ ] **Step 9: 문서 변경 커밋**

```bash
git add README.md docs/protocol.md docs/known-issues.md docs/project-status.md
git commit -m "문서: 문자열 payload와 UTF-8 계약 반영"
```

- [ ] **Step 10: PR 전 최종 확인**

```bash
git status --short --branch
git log --oneline main..HEAD
git diff --check main...HEAD
```

Expected: worktree가 clean이고 protocol, server, Qt, Linux 도구, 문서 커밋이
순서대로 존재하며 whitespace 오류가 없습니다. PR 설명에는 `문제`, `해결방법`,
`예상되는 문제`와 실제 실행한 검증 결과를 각각 기록합니다.
