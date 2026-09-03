# 문자열 payload와 UTF-8 계약 설계

날짜: 2026-09-03

## 배경

현재 문자열 응답과 방 이벤트는 `|`로 항목을 나누고 `=`로 key와 value를
구분합니다. 사용자 이름이 `|session_id=...` 같은 문자열을 포함하면 수신 측의
단순 구분자 검색이 원래 값과 메타데이터를 구별할 수 없습니다. 또한 서버는
사용자 이름, 방 이름과 채팅의 UTF-8 유효성을 검사하지 않으며 방 이름을
32바이트에서 그대로 잘라 UTF-8 code point를 손상시킬 수 있습니다.

이 변경은 기존 4바이트 패킷 header와 packet type을 유지하면서 문자열 값의
경계, 허용 문자, 크기와 오류 처리를 명확히 정의합니다. 서버, Qt 클라이언트,
콘솔 도구와 부하 도구는 같은 protocol library 구현을 사용합니다.

## 목표

- 문자열 입력이 유효한 UTF-8인지 모든 진입점에서 같은 규칙으로 검증합니다.
- 구조화 payload의 문자열 값에 예약문자가 포함돼도 원래 값을 정확히
  복원합니다.
- 모든 문자열 상한은 UTF-8 byte 단위로 정의하고 code point 중간을 자르지
  않습니다.
- 어떤 허용 입력도 server broadcast를 4096바이트 패킷 상한보다 크게 만들지
  않도록 고정 상한을 사용합니다.
- 잘못된 입력과 잘못된 응답이 상태를 부분적으로 변경하지 않게 합니다.

## 범위 밖

- 패킷 header, packet type 번호와 위치 update의 8바이트 binary 형식 변경
- Unicode NFC/NFKC 정규화와 비 ASCII 대소문자 통합
- 프로토콜 version negotiation과 구형 client 자동 호환
- 인증, TLS, 방 목록과 채팅 기록 영속화

## 문자열 종류

### 단일 값 요청

`LOGIN_REQ`, `CREATE_ROOM_REQ`, `CHAT_REQ` payload는 하나의 값만 가지므로 raw
UTF-8 byte sequence로 전송합니다. 이 요청들에는 percent encoding을 적용하지
않습니다. 따라서 사용자 입력에 있는 `%`, `|`, `=`는 원래 문자 그대로
서버에 도착합니다.

`JOIN_ROOM_REQ`는 ASCII 10진수, `LEAVE_ROOM_REQ`와 `PING`은 빈 payload,
`POSITION_UPDATE`는 기존 binary 형식을 유지합니다.

### 구조화 응답과 broadcast

서버가 생성하는 로그인·방 응답과 `ROOM_BROADCAST`는 다음 grammar를
사용합니다.

```text
payload  = [status "|"] field *("|" field)
status   = "OK"
field    = key "=" value
key      = 1*(ALPHA / DIGIT / "_")
value    = *(unreserved-utf8 / percent-escape)
percent-escape = "%" HEXDIG HEXDIG
```

key, status와 protocol event 이름은 ASCII 상수입니다. 같은 key가 두 번
등장하거나, field에 `=`가 없거나, 빈 key가 있거나, percent escape가 완성되지
않은 payload는 잘못된 구조화 payload입니다. field 순서는 문서의 예시와 현재
서버 출력 순서를 유지하지만 수신 측은 key로 값을 찾습니다.

## percent encoding

문자열 value를 UTF-8 byte sequence로 만든 뒤 다음 세 ASCII byte만
인코딩합니다.

| 원래 byte | 출력 |
| --- | --- |
| `%` | `%25` |
| `|` | `%7C` |
| `=` | `%3D` |

인코더는 대문자 16진수를 출력합니다. 디코더는 대문자와 소문자 16진수를 모두
허용하고, percent decoding이 끝난 결과를 다시 UTF-8로 검증합니다. 다른
UTF-8 byte는 그대로 유지하므로 한글과 다른 비 ASCII 문자열은 읽을 수 있는
형태로 남습니다.

예를 들어 표시 이름 `kim|role=admin%`은 다음과 같이 전송됩니다.

```text
name=kim%7Crole%3Dadmin%25
```

채팅 message도 동일하게 인코딩합니다. message를 마지막 field로 취급하는
예외 규칙을 두지 않으므로 field 순서가 바뀌어도 안전하게 해석할 수 있습니다.

## UTF-8과 문자 허용 규칙

유효성 검사는 RFC 3629 범위의 Unicode scalar value만 허용합니다. 다음 입력은
거부합니다.

- 잘린 multibyte sequence
- continuation byte가 없거나 잘못된 sequence
- overlong encoding
- UTF-16 surrogate 범위 `U+D800`~`U+DFFF`
- `U+10FFFF`보다 큰 값
- ASCII control byte `U+0000`~`U+001F`와 `U+007F`

Unicode normalization은 수행하지 않습니다. 같은 모양이더라도 byte sequence가
다르면 서로 다른 이름으로 취급될 수 있습니다. 로그인 조회 key는 기존처럼
ASCII 앞뒤 공백을 제거하고 ASCII `A`~`Z`만 소문자로 변환합니다. 방 이름도
ASCII 앞뒤 공백을 제거하지만 대소문자는 변경하지 않습니다. 채팅은 앞뒤
공백을 보존합니다.

## 고정 byte 상한

상한은 trim 이후, percent encoding 이전의 raw UTF-8 byte 수에 적용합니다.
검증에 실패하면 일부를 잘라 사용하지 않고 요청 전체를 거부합니다.

| 문자열 | 최소 | 최대 | 비고 |
| --- | ---: | ---: | --- |
| 사용자 표시 이름 | 1 | 32 | ASCII trim 및 ASCII case-fold 적용 |
| 방 이름 | 1 | 32 | ASCII trim, 빈 이름의 기본값 없음 |
| 채팅 message | 0 | 1291 | 공백 보존 |

채팅 상한 1291바이트는 최악의 percent encoding에서도 패킷 전체가
4096바이트를 넘지 않게 계산한 값입니다. 최대 10자리 room id, canonical UUID,
최대 20자리 session id, 32바이트 이름이 모두 예약문자이고 채팅의 모든 byte도
예약문자인 경우 구조화 CHAT broadcast envelope는 다음 크기를 사용합니다.

```text
최대 payload                4092 = 4096 - 4바이트 header
CHAT 고정 field와 최대 이름  217
message에 남는 encoded byte 3875 = 4092 - 217
최대 raw message             1291 = floor(3875 / 3)
```

이 상한은 단순하고 재현 가능한 protocol 상수로 둡니다. 입력 내용에 따라 더 긴
채팅을 조건부 허용하지 않습니다.

## protocol library 경계

`libs/protocol`에 다음 책임을 가진 작은 단위를 추가합니다.

- UTF-8과 ASCII control byte 검증
- 문자열 value의 percent encode/decode
- status와 key/value field를 가진 구조화 payload 생성
- 구조화 payload parsing, duplicate key 확인과 required field 조회

이 코드는 Qt나 server-core에 의존하지 않습니다. public API는 UTF-8 byte를
`std::string_view`로 받고 소유 결과를 `std::string` 또는 field collection으로
반환합니다. 잘못된 UTF-8, escape와 grammar는 기존 `ProtocolError`로
보고합니다.

`PacketCodec`은 계속 TCP frame만 담당합니다. 문자열 field 책임을
`PacketCodec`에 섞지 않아 binary packet 처리와 text schema 처리를 분리합니다.

## 서버 처리 순서

1. `MessageRouter`가 text 요청을 raw byte 문자열로 읽습니다.
2. 공용 validator로 UTF-8, control byte와 raw byte 상한을 확인합니다.
3. 사용자 이름과 방 이름은 검증 전에 ASCII trim 범위를 계산하되 원본을
   변경하거나 자르지 않습니다.
4. 검증이 성공한 뒤에만 repository 또는 `RoomService`를 호출합니다.
5. 응답과 broadcast의 동적 문자열 field는 공용 builder로 인코딩합니다.

검증 실패는 다음 기존 방식의 `ERROR` packet으로 응답하며 연결은 유지합니다.

| 요청 | 오류 문구 |
| --- | --- |
| `LOGIN_REQ` | `invalid user name` |
| `CREATE_ROOM_REQ` | `invalid room name` |
| `CHAT_REQ` | `invalid chat message` 또는 `chat message too large` |

잘못된 요청은 사용자, 방, membership과 채팅 broadcast를 변경하지 않습니다.
`RoomService`의 빈 방 이름 기본값과 32바이트 강제 절단은 제거합니다. 서비스
계층은 이미 검증된 방 이름을 받는다는 계약을 명시하되 직접 호출 테스트에서도
잘못된 값이 저장되지 않도록 방어 검증을 유지합니다.

## 클라이언트 처리

Qt `ClientController`는 `QString::indexOf('|')`로 field를 찾지 않고 공용 parser의
결과를 사용합니다. required field가 없거나 payload가 잘못됐으면 state를
변경하지 않고 protocol 오류를 chat log에 추가합니다. 수신 packet byte를 먼저
검증한 뒤 `QString::fromUtf8`로 변환하므로 replacement character에 의한 조용한
데이터 손실을 허용하지 않습니다.

콘솔 client는 구조화 payload를 parse하고 percent-decoded value를 사람이 읽을
수 있게 출력합니다. 부하 시나리오 client는 `room_id` 같은 required field를
같은 parser로 읽으며 잘못된 응답을 측정 실패로 처리합니다.

요청을 보낼 때 Qt와 콘솔 client는 단일 값 요청을 percent encode하지 않습니다.
대신 송신 전 동일한 UTF-8·control·byte 상한 검사를 수행해 서버와 같은 오류를
즉시 사용자에게 안내합니다. 서버 검증은 신뢰 경계이므로 항상 유지합니다.

## 호환성

패킷 framing과 type 번호는 호환되지만 예약문자를 포함한 동적 value 표현은
변경됩니다. 변경 전 client는 `%7C` 같은 값을 그대로 표시하고, 변경 후 client는
변경 전 server가 보낸 literal `%7C`를 decoding할 수 있으므로 두 version을
혼용하지 않습니다. 이 저장소의 server와 client를 같은 변경에서 갱신하고
배포 단위도 맞춥니다.

예약문자가 없는 기존 payload는 byte 단위로 동일합니다. protocol version
negotiation은 별도 로드맵 항목이며 이번 변경에 추가하지 않습니다.

## 테스트 전략

### protocol 단위 테스트

- ASCII와 한글 value 왕복
- `%`, `|`, `=` 각각과 조합의 canonical encoding
- 소문자 percent escape decoding
- 잘린 escape, non-hex escape, 빈 key와 duplicate key 거부
- 잘린 UTF-8, overlong encoding, surrogate와 범위 초과 거부
- ASCII control byte 거부

### server-core 테스트

- 32바이트 이하 한글 이름과 방 이름 승인
- code point 경계를 넘는 33바이트 입력을 절단하지 않고 거부
- 구분자를 포함한 사용자 이름이 모든 응답과 broadcast에서 왕복
- 1291바이트 채팅의 최악 escape가 4096바이트 이하로 encoding
- 1292바이트 채팅 거부와 broadcast 미발생
- 잘못된 UTF-8 요청 뒤에도 같은 session의 정상 요청 처리

### client와 통합 테스트

- Qt가 escaped name과 message를 원래 문자열로 표시
- 잘못된 응답에서 Qt state를 변경하지 않고 오류 표시
- 콘솔과 scenario client가 escaped field를 decode
- Linux 실제 server에서 구분자를 포함한 로그인·방·채팅 왕복

## 문서와 완료 기준

- `docs/protocol.md`에 grammar, encoding, UTF-8, 상한과 오류를 반영합니다.
- `README.md`의 client 사용 예시와 제한을 갱신합니다.
- 완료 후 `docs/known-issues.md`에서 해결된 문자열 payload 항목을 제거하고
  `docs/project-status.md`의 완료 이력과 다음 우선순위를 갱신합니다.
- core, Linux, Qt build와 test, `format-check`, 가능한 경우 `tidy-check`,
  ASan/UBSan 검증을 통과합니다.
