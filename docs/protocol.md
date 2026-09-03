# 패킷 프로토콜

서버와 클라이언트는 TCP 연결 위에서 자체 패킷 형식을 사용합니다.
TCP는 바이트의 순서만 보장하고 메시지 경계를 알려주지 않으므로, 각
메시지 앞에 크기와 종류를 기록한 헤더를 붙입니다.

## 패킷 구성

모든 패킷은 4바이트 헤더와 선택적인 payload로 구성됩니다.

```text
0               1               2               3
+---------------+---------------+---------------+---------------+
|       전체 패킷 크기          |          패킷 종류            |
+---------------+---------------+---------------+---------------+
|                         payload ...                           |
+---------------------------------------------------------------+
```

```cpp
struct PacketHeader {
  std::uint16_t size;
  std::uint16_t type;
};
```

- `size`: 4바이트 헤더를 포함한 전체 패킷 크기
- `type`: 요청이나 응답의 종류
- 정수는 big-endian, 즉 network byte order로 저장
- 최소 크기: 4바이트
- 최대 크기: 4096바이트

예를 들어 payload가 없는 `PING`은 크기가 4이고 종류가 30입니다.

```text
00 04 00 1e
```

## 패킷 종류

| 값 | 이름 | 방향 | payload |
| ---: | --- | --- | --- |
| 1 | `LOGIN_REQ` | 클라이언트 → 서버 | UTF-8 사용자 이름 |
| 2 | `LOGIN_RES` | 서버 → 클라이언트 | 로그인 결과 |
| 10 | `CREATE_ROOM_REQ` | 클라이언트 → 서버 | UTF-8 방 이름 |
| 11 | `CREATE_ROOM_RES` | 서버 → 클라이언트 | 방 생성 결과 |
| 12 | `JOIN_ROOM_REQ` | 클라이언트 → 서버 | 10진수 방 번호 문자열 |
| 13 | `JOIN_ROOM_RES` | 서버 → 클라이언트 | 방 참가 결과 |
| 14 | `LEAVE_ROOM_REQ` | 클라이언트 → 서버 | 없음 |
| 15 | `LEAVE_ROOM_RES` | 서버 → 클라이언트 | 방 나가기 결과 |
| 20 | `CHAT_REQ` | 클라이언트 → 서버 | UTF-8 채팅 메시지 |
| 21 | `POSITION_UPDATE` | 클라이언트 → 서버 | `float x`, `float y` |
| 22 | `ROOM_BROADCAST` | 서버 → 클라이언트 | 방 이벤트 문자열 |
| 30 | `PING` | 클라이언트 → 서버 | 없음 |
| 31 | `PONG` | 서버 → 클라이언트 | `PONG` |
| 100 | `ERROR` | 서버 → 클라이언트 | 오류 설명 |

## 문자열 payload 계약

`LOGIN_REQ`, `CREATE_ROOM_REQ`, `CHAT_REQ`는 하나의 값만 가지므로 raw UTF-8
byte sequence를 보냅니다. 요청에는 percent encoding을 적용하지 않습니다.
`JOIN_ROOM_REQ`는 ASCII 10진수, `LEAVE_ROOM_REQ`와 `PING`은 빈 payload입니다.

로그인·방 응답과 `ROOM_BROADCAST`는 다음 구조화 payload grammar를 사용합니다.

```text
payload  = [status "|"] field *("|" field)
status   = "OK"
field    = key "=" value
key      = 1*(ALPHA / DIGIT / "_")
value    = *(unreserved-utf8 / percent-escape)
percent-escape = "%" HEXDIG HEXDIG
```

`unreserved-utf8`은 유효한 UTF-8 text 중 ASCII `%`, `|`, `=`가 아닌 byte입니다.
동적 value의 UTF-8 byte 중 `%`, `|`, `=`는 각각 `%25`, `%7C`, `%3D`로
인코딩합니다. encoder는 대문자 16진수를 출력하고 decoder는 대·소문자를 모두
받습니다. 다른 UTF-8 byte는 그대로 유지합니다. 예를 들어
`kim|role=admin%`는 `kim%7Crole%3Dadmin%25`가 됩니다. field 순서는 의미가
없으며 수신자는 key로 조회해야 합니다.

빈 key, `=`가 없거나 둘 이상인 field, 중복 key, 빈 segment, 잘못된 percent
escape는 잘못된 구조화 payload입니다. 각 packet schema의 필수 field가 없거나
status가 기대와 달라도 수신자는 packet 전체를 거부해야 하며, 일부 field만
상태에 반영하면 안 됩니다.

모든 text는 RFC 3629 범위의 Unicode scalar value로 구성된 UTF-8이어야 합니다.
잘린 sequence, overlong encoding, UTF-16 surrogate, `U+10FFFF` 초과 값과 ASCII
control `U+0000`~`U+001F`, `U+007F`은 거부합니다. Unicode normalization은
수행하지 않습니다.

문자열 상한은 ASCII trim 이후, percent encoding 이전의 raw UTF-8 byte 수로
계산합니다. 상한을 넘으면 자르지 않고 요청 전체를 거부합니다.

| 문자열 | 허용 범위 | 공백 처리 |
| --- | ---: | --- |
| 사용자 표시 이름 | 1~32바이트 | 앞뒤 ASCII 공백 제거 |
| 방 이름 | 1~32바이트 | 앞뒤 ASCII 공백 제거 |
| 채팅 메시지 | 0~1291바이트 | 원문 공백 보존 |

채팅 상한 1291바이트는 이름과 메시지의 모든 byte가 예약문자인 최악의 percent
encoding에서도 4바이트 header를 포함한 packet이 4096바이트를 넘지 않는 고정
상한입니다. 입력 내용에 따라 더 긴 채팅을 조건부로 허용하지 않습니다.

## payload 예시

### 로그인 성공

```text
OK|user_id=00000000-0000-0000-0000-000000000001|session_id=10|name=kim%7Crole%3Dadmin%25
```

`user_id`는 소문자 canonical UUID 문자열이며 연결마다 바뀌는 `session_id`와
구분합니다. `LOGIN_REQ` 이름은 ASCII 앞뒤 공백을 제거하고 ASCII 대문자를
소문자로 바꾼 1~32바이트 조회 키로 사용합니다. 같은 저장소에 같은 조회 키로
재접속하면 같은 `user_id`를 반환합니다. 표시 이름의 비 ASCII UTF-8 바이트는
변경하지 않습니다. 위 예시의 표시 이름을 decode하면
`kim|role=admin%`입니다.

현재 이름 기반 로그인은 개발용 식별 절차이며 사용자 인증이 아닙니다. 이름을
아는 다른 클라이언트도 같은 사용자로 식별될 수 있으므로 실제 서비스에서는
별도의 인증 수단을 추가해야 합니다.

### 방 생성 성공

```text
OK|event=CREATE_ROOM|room_id=1|user_id=00000000-0000-0000-0000-000000000001|session_id=10|name=alice
```

### 방 참가 성공

```text
OK|event=JOIN_ROOM|room_id=1|user_id=00000000-0000-0000-0000-000000000002|session_id=11|name=bob
```

### 채팅 broadcast

```text
event=CHAT|room_id=1|user_id=00000000-0000-0000-0000-000000000001|session_id=10|name=alice|message=hello%7Cthere
```

### 위치 broadcast

```text
event=POSITION|room_id=1|user_id=00000000-0000-0000-0000-000000000001|session_id=10|name=alice|x=10.5|y=22
```

### 오류

```text
user is not logged in
```

## 위치 payload

`POSITION_UPDATE` 요청만 문자열이 아닌 8바이트 바이너리 payload를
사용합니다.

```text
앞 4바이트: IEEE-754 float x
뒤 4바이트: IEEE-754 float y
```

두 값의 비트 패턴은 각각 big-endian으로 저장합니다.

## 기본 사용 순서

1. TCP 연결을 만듭니다.
2. `LOGIN_REQ`로 로그인합니다.
3. `CREATE_ROOM_REQ` 또는 `JOIN_ROOM_REQ`로 방에 들어갑니다.
4. `CHAT_REQ`와 `POSITION_UPDATE`를 보냅니다.
5. `LEAVE_ROOM_REQ`로 방에서 나갑니다.

`PING`은 로그인하지 않아도 사용할 수 있습니다. 로그인이나 방 참가가
필요한 명령을 순서에 맞지 않게 보내면 서버는 `ERROR` 패킷을
반환합니다.

한 TCP 연결에서는 `LOGIN_REQ`가 한 번만 성공합니다. 로그인한 연결이 같은
이름이나 다른 이름으로 다시 로그인하면 서버는 `user is already logged in`
오류를 반환하며 user id, 이름과 방 참가 상태를 변경하지 않습니다.

방에 참가한 사용자는 `LEAVE_ROOM_REQ`가 성공한 뒤에만 새 방을 만들거나 다른
방에 참가할 수 있습니다. 방 안에서 `CREATE_ROOM_REQ` 또는 다른 방 번호의
`JOIN_ROOM_REQ`를 보내면 `leave current room first` 오류를 반환합니다. 대상
방이 존재하지 않더라도 현재 방 참가 여부를 먼저 검사합니다. 이미 참가한 방
번호로 다시 참가하면 `user is already in room` 오류를 반환합니다. 거부된 방
요청은 기존 방 상태를 변경하거나 참가·퇴장 broadcast를 만들지 않습니다.

잘못된 `LOGIN_REQ`, `CREATE_ROOM_REQ`, `CHAT_REQ`는 각각 `invalid user name`,
`invalid room name`, `invalid chat message` 오류를 반환합니다. 채팅은 UTF-8
검사보다 byte 상한을 먼저 확인하므로 1291바이트를 넘으면 내용과 관계없이
`chat message too large`를 반환합니다.
payload가 비어 있어야 하는 `LEAVE_ROOM_REQ`와 `PING`에 데이터가 있으면 각각
`invalid leave room request`, `invalid ping request`를 반환합니다.
서버는 `ERROR` packet을 보낸 뒤 연결을 유지하며, 거부된 요청으로 사용자·방·
membership 상태를 바꾸거나 broadcast를 만들지 않습니다.

## TCP에서 패킷을 읽는 방법

TCP의 한 번의 `recv`와 한 패킷은 일치하지 않습니다.

- 한 패킷이 여러 번의 `recv`로 나뉘어 들어올 수 있습니다.
- 여러 패킷이 한 번의 `recv`에 합쳐질 수 있습니다.

`PacketCodec`은 아직 완성되지 않은 바이트를 내부 버퍼에 보관합니다.
먼저 4바이트 헤더를 확인하고, `size`만큼 바이트가 모였을 때만 완성된
패킷을 반환합니다. 남은 바이트는 다음 `feed` 호출까지 유지합니다.

다음 경우에는 `ProtocolError`가 발생합니다.

- 헤더의 크기가 4보다 작음
- 전체 패킷이 4096바이트를 초과함
- 위치 payload가 정확히 8바이트가 아님
- 인코딩할 payload가 최대 크기를 초과함
