# 채팅 영속성 아키텍처 설계

## 목적

현재 서버의 사용자, 방과 참가 상태는 프로세스 메모리에만 존재합니다. TCP
연결이 끊기거나 서버가 재시작되면 사용자와 방이 사라지고, 한 세션은 한 방에만
참가할 수 있으며 메시지 기록도 남지 않습니다.

이 문서는 다음 목표를 만족하는 영속성 경계와 단계적 도입 방식을 정의합니다.

- TCP 연결과 무관한 영구 사용자 식별자를 사용합니다.
- 한 사용자가 여러 채팅방에 가입하고 재접속 후 방 목록을 복구할 수 있습니다.
- 메시지는 저장에 성공한 뒤에만 성공한 것으로 간주하고 전달합니다.
- 사용자와 방 관계는 관계형 무결성을 유지합니다.
- 초기 운영 복잡도를 제한하면서 메시지 저장소를 향후 Cassandra 또는
  ScyllaDB로 교체할 수 있습니다.
- DB 또는 개별 요청 실패가 정상 세션이나 서버 전체 종료로 확대되지 않습니다.

## 현재 상태와 문제

`RoomService`는 `next_user_id_`, 세션별 사용자와 현재 방을 모두 메모리에
보관합니다. 로그인할 때마다 새 user id가 생기며, `Room`의 구성원도 영구
사용자가 아니라 session id를 기준으로 관리합니다. 빈 방은 제거되고 채팅
요청은 메시지 객체를 만들거나 저장하지 않은 채 현재 구성원에게 바로
broadcast됩니다.

따라서 현재의 session id 중심 모델을 그대로 DB에 옮기지 않습니다. 연결,
계정, 방 가입과 현재 활동을 서로 다른 수명의 상태로 분리해야 합니다.

## 핵심 결정

### PostgreSQL로 시작

첫 영속성 구현은 사용자, 방, 멤버십, 읽음 위치와 메시지를 모두 PostgreSQL에
저장합니다. PostgreSQL은 이 단계에서 필요한 고유성 제약, 외래 키, 여러
레코드의 트랜잭션과 조회 조합을 한 저장소에서 제공합니다.

메시지 접근은 `MessageStore` 포트 뒤에 격리합니다. 실제 쓰기 처리량, 데이터
용량 또는 다중 지역 가용성 요구가 PostgreSQL 구성의 한계를 만들었을 때
메시지만 Cassandra 또는 ScyllaDB로 이전합니다. 예상 규모만으로 처음부터 두
DB를 운영하지 않습니다.

PostgreSQL은 큰 메시지 테이블을 선언적 파티셔닝으로 나눌 수 있으므로, 이전
단계에서도 시간 또는 hash 파티셔닝을 적용할 수 있습니다. 자세한 기능은
[PostgreSQL 파티셔닝 문서](https://www.postgresql.org/docs/current/ddl-partitioning.html)를
기준으로 합니다.

### Cassandra와 ScyllaDB의 위치

Cassandra 계열 저장소는 방별로 계속 추가되는 메시지를 큰 규모로 수평
확장하는 후보입니다. 이 계열에서는 효율적인 조회가 partition key를 포함해야
하며 cross-partition transaction, 분산 join과 foreign key를 제공하지
않습니다. 따라서 사용자, 방과 멤버십은 이전 이후에도 PostgreSQL을 기준
저장소로 유지합니다. 관련 제약은
[Apache Cassandra 아키텍처](https://cassandra.apache.org/doc/latest/cassandra/architecture/overview.html)를
기준으로 합니다.

메시지를 영구 보존하므로 TTL 중심 설계를 전제로 하지 않습니다. Cassandra
5를 채택하면 신규 workload 전반에 권장되는 Unified Compaction Strategy를
먼저 검증하고, 실제 write/read 비율과 partition 크기로 설정을 조정합니다.
[Apache Cassandra UCS 문서](https://cassandra.apache.org/doc/stable/cassandra/managing/operating/compaction/ucs.html)를
참고합니다.

Cassandra와 ScyllaDB 중 하나를 지금 확정하지 않습니다. 동일한 `MessageStore`
계약, 데이터셋과 부하 시나리오로 다음 항목을 측정한 뒤 선택합니다.

- 지속 write 처리량과 p95/p99 지연
- 최근 메시지와 이전 페이지 조회 지연
- 큰 방 하나가 만드는 hot partition 영향
- 노드 장애와 복구 중의 가용성
- C++ driver의 지원 범위와 통합 안정성
- cluster, repair, compaction, backup 운영 비용

### 저장 후 전달

메시지는 영속 저장 성공 이후에만 확정 ID를 포함해 broadcast합니다. 클라이언트가
성공 응답이나 broadcast를 관측한 메시지는 저장소에도 존재해야 합니다.

DB timeout은 결과가 불명확할 수 있으므로 클라이언트가 생성한
`client_message_id`를 멱등성 키로 사용합니다. 같은 사용자가 같은
`client_message_id`로 재시도하면 새 메시지를 추가하지 않고 최초 저장 결과를
반환합니다.

### 방 안의 메시지 순서

모든 클라이언트가 같은 방의 메시지를 같은 순서로 관측해야 합니다. 첫 단일
서버 단계에서는 방별 command sequencer가 append 시작과 완료 게시 순서를
관리합니다. 먼저 시작한 메시지의 DB 완료가 늦더라도 뒤 메시지를 먼저
broadcast하지 않습니다. 앞 메시지가 실패하면 오류를 확정한 다음 다음 완료를
게시합니다.

저장소의 정렬 기준은 `MessageId`의 byte 순서로 고정합니다. UUIDv7의 시간
부분과 동일 시각의 단조 증가 값을 사용해 한 서버에서 생성 순서와 정렬 순서가
일치하게 합니다. 향후 여러 서버가 같은 방의 쓰기를 처리할 때는 방을 한
instance에 소유시키거나 별도 sequencer를 도입해야 합니다. 이 결정 전까지
여러 instance의 timestamp만으로 엄격한 방 순서를 보장한다고 가정하지
않습니다.

## 상태의 수명과 기준 저장소

| 상태 | 수명 | 기준 저장소 |
| --- | --- | --- |
| 사용자 ID와 표시 이름 | 영구 | PostgreSQL |
| 방 정보 | 영구 | PostgreSQL |
| 방 멤버십과 역할 | 영구 | PostgreSQL |
| 사용자별 방 읽음 위치 | 영구 | PostgreSQL |
| 메시지와 수정·삭제 상태 | 영구 | 초기 PostgreSQL, 향후 `MessageStore` 구현 |
| TCP session과 packet 순서 | 연결 동안 | 서버 메모리 |
| 온라인 상태와 heartbeat | 짧은 수명 | 서버 메모리, 다중 서버 단계에서 별도 검토 |
| 현재 화면에서 연 방 | session 동안 | 서버 메모리 |
| 위치와 typing 상태 | 짧은 수명 | 서버 메모리 |

session id는 DB의 사용자 기본 키가 아니며 재접속 때 재사용하지 않습니다. 한
영구 사용자에는 여러 기기의 session이 동시에 연결될 수 있습니다.

## 도메인 모델

### 식별자

- `UserId`와 `RoomId`는 session 및 DB 구현과 무관한 불투명 UUID입니다.
- `MessageId`는 여러 서버에서도 생성할 수 있고 시간순 page 경계로 사용할 수
  있는 UUIDv7 계열의 불투명 식별자입니다.
- `SessionId`는 현재 연결 하나만 식별하는 별도 타입입니다.
- `ClientMessageId`는 클라이언트가 만들며 한 사용자의 재시도 범위에서
  유일합니다.

프로토콜과 도메인 API는 구체 DB의 sequence나 CQL 타입을 직접 노출하지
않습니다. UUID의 wire 표현과 유효성 규칙은 프로토콜 변경 단계에서 별도로
확정합니다.

### 사용자와 개발용 로그인

첫 단계에서는 비밀번호나 token 인증을 추가하지 않습니다. 로그인 이름을
정규화해 기존 사용자를 찾고, 없으면 새 영구 사용자를 생성합니다.
`normalized_name`에는 고유성 제약을 두지만 이 방식은 개발용 식별 절차일 뿐
신뢰 가능한 인증으로 취급하지 않습니다.

향후 계정 인증을 추가할 때 기존 `UserId`를 유지하고 인증 credential과
identity provider 연결만 추가합니다. 표시 이름 변경도 영구 ID를 바꾸지
않습니다.

### 방과 멤버십

방은 구성원이 일시적으로 0명이 되어도 삭제하지 않습니다. 사용자는
`room_memberships`를 통해 여러 방에 가입할 수 있습니다. 연결된 session에서
현재 열어 둔 방은 UI와 실시간 event 범위를 나타낼 뿐 멤버십을 바꾸지
않습니다.

멤버십 상태 변경과 메시지 전송 권한 검사는 같은 방의 명령 순서에서 관측
가능한 순서를 유지해야 합니다. 첫 단일 서버 단계에서는 worker가 관리하는
방 상태와 DB 완료 event를 순서화합니다. 다중 서버로 확장할 때는 멤버십
version과 instance 간 invalidation 전달을 추가해 stale cache 권한을
제한합니다.

## 논리 스키마

정확한 SQL migration은 구현 계획에서 작성하되 다음 제약을 유지합니다.

### `users`

| 열 | 의미 |
| --- | --- |
| `user_id` | 영구 기본 키 |
| `normalized_name` | 개발용 로그인 조회 키, unique |
| `display_name` | 화면 표시 이름 |
| `created_at` | 생성 시각 |
| `updated_at` | 마지막 변경 시각 |

### `rooms`

| 열 | 의미 |
| --- | --- |
| `room_id` | 영구 기본 키 |
| `room_type` | group 또는 향후 direct 등 방 종류 |
| `name` | 방 이름 |
| `created_by` | 생성 사용자 foreign key |
| `created_at` | 생성 시각 |
| `closed_at` | 명시적으로 닫은 시각, nullable |

### `room_memberships`

| 열 | 의미 |
| --- | --- |
| `room_id`, `user_id` | 복합 기본 키와 각 foreign key |
| `role` | owner, member 등 역할 |
| `state` | active, left 등 가입 상태 |
| `joined_at` | 가입 시각 |
| `left_at` | 탈퇴 시각, nullable |

재가입 이력을 한 행에 덮어쓸지 별도 이력 테이블로 남길지는 감사 요구가 생길
때 확장합니다. 첫 단계의 현재 멤버십 판정은 위 행을 기준으로 합니다.

### `room_read_cursors`

읽음 위치는 멤버십보다 갱신 빈도가 높으므로 별도 테이블로 둡니다.

| 열 | 의미 |
| --- | --- |
| `room_id`, `user_id` | 복합 기본 키와 membership 참조 |
| `last_read_message_id` | 마지막으로 읽은 확정 메시지 |
| `updated_at` | 마지막 갱신 시각 |

cursor는 뒤로 이동하지 않도록 조건부 갱신합니다. 읽지 않은 수는 매번 전체
메시지를 count하는 방식에 의존하지 않고, 방별 확정 순번이나 별도 요약
projection을 추가하는 단계에서 계산 방식을 확정합니다.

### `messages`

| 열 | 의미 |
| --- | --- |
| `message_id` | 시간순 정렬 가능한 영구 식별자 |
| `room_id` | 소속 방 foreign key |
| `sender_user_id` | 작성 사용자 foreign key |
| `client_message_id` | 재시도 멱등성 키 |
| `body` | 첫 단계의 text 본문 |
| `created_at` | 서버가 확정한 생성 시각 |
| `edited_at` | 수정 시각, nullable |
| `deleted_at` | 삭제 시각, nullable |

`(sender_user_id, client_message_id)`에는 unique 제약을 둡니다. 방별
`(room_id, message_id)` index로 최근 및 이전 page를 조회합니다. 삭제는 즉시
본문을 노출하지 않는 논리 tombstone으로 표현하고, 물리 삭제와 감사 보존
정책은 별도 운영 정책으로 확장합니다.

## 컴포넌트와 의존성

```text
apps/rss-server
    │ concrete adapter 조립과 설정
    ├──────────────────────────────┐
    ▼                              ▼
server-net-linux               persistence-postgres
    │                              │
    └──────────► server-core ◄─────┘
                       │
                       ▼
                    protocol
```

- `libs/server-core`는 저장소 port와 도메인 타입을 정의하고 계속
  `protocol`에만 의존합니다.
- 새 PostgreSQL adapter library가 `server-core`의 port를 구현하며 DB driver와
  migration 세부 사항을 소유합니다.
- `apps/`에서 connection pool, repository와 application service를 조립합니다.
- `server-net-linux`는 DB 세부 사항을 알지 못하며 기존처럼 I/O와 session
  lifecycle만 담당합니다.
- 테스트용 in-memory adapter를 유지해 도메인 테스트가 외부 DB 없이 빠르게
  실행되게 합니다. PostgreSQL adapter는 별도 integration test로 검증합니다.

구체 library 이름과 CMake target은 기존 명명 규칙을 확인해 구현 계획에서
확정합니다.

## 비동기 실행과 순서

DB 호출은 I/O 스레드와 session worker를 기다리게 하지 않습니다. 제한된 DB
명령 queue, 전용 executor와 완료 queue를 둡니다.

```text
I/O thread -> inbound queue -> session worker
                                  │ DB command
                                  ▼
                           bounded DB queue
                                  ▼
                             DB executor
                                  │ completion
                                  ▼
                         session-ordered completion
                                  │ outbound message
                                  ▼
                              I/O thread
```

현재 `SessionEventHandler::handle()`은 동기 호출에서 응답을 완성하므로 비동기
DB 완료를 표현할 수 있게 command 시작과 completion 처리를 분리해야 합니다.
worker가 future를 blocking wait하는 방식은 사용하지 않습니다. 같은 session의
후속 명령이 미완료 영속 명령을 추월하지 않도록 session별 순서를 유지하되,
서로 다른 session은 계속 처리합니다.

미완료 session은 worker thread를 점유하는 대신 상태를 `AwaitingPersistence`로
표시합니다. DB completion을 새 내부 event로 session 순서 queue에 다시 넣어
처리를 재개합니다. 메시지 broadcast는 session 순서뿐 아니라 앞에서 정의한
방별 게시 순서도 통과해야 합니다.

DB queue는 설정 가능한 고정 용량을 가집니다. 포화되면 명령을 무제한
적재하지 않고 요청 session에 재시도 가능한 `server_busy` 오류를 반환합니다.
DB 완료 알림도 I/O thread의 socket 소유권 규칙을 우회하지 않으며 모든
socket write와 close는 계속 I/O thread에서만 수행합니다.

## 요청 흐름

### 로그인

1. payload의 이름 형식과 길이를 검사하고 정규화합니다.
2. `UserRepository`가 `normalized_name`으로 사용자를 조회하거나 원자적으로
   생성합니다.
3. 완료된 `UserId`를 현재 `SessionId`와 메모리에서 연결합니다.
4. 성공 응답에 영구 user id를 포함합니다.
5. DB 실패 시 로그인 요청만 실패하며 연결은 유지합니다.

동시 최초 로그인은 unique 제약과 충돌 후 재조회로 하나의 사용자만 생성되게
합니다.

### 방 생성과 가입

1. 로그인된 영구 사용자인지 확인합니다.
2. 방 생성은 `rooms`와 owner membership을 한 PostgreSQL transaction에
   저장합니다.
3. 가입은 방 상태와 기존 membership을 검사하고 active 상태로 저장합니다.
4. commit 이후 session의 활성 방 상태와 broadcast 대상 상태를 갱신합니다.
5. rollback 또는 timeout이면 성공 event를 게시하지 않습니다.

사용자가 화면에서 다른 방을 여는 동작은 멤버십 생성과 분리합니다. 기존의
한 방 참가 상태를 장기적으로 `active_room_by_session` 의미로 바꾸고, 영구
멤버십은 별도 모델로 관리합니다.

### 메시지 전송

1. packet 형식, 본문 크기, 로그인과 active membership을 검사합니다.
2. server가 `MessageId`와 생성 시각을 정하고 `MessageDraft`를 만듭니다.
3. `MessageStore::append()`를 비동기로 요청합니다.
4. 저장 성공 후 확정 메시지를 sender와 해당 방의 접속 session에
   broadcast합니다.
5. 이미 존재하는 `client_message_id`이면 최초 확정 결과를 동일하게
   반환합니다.
6. 실패 또는 queue 포화면 요청 session에만 오류를 반환하고 broadcast하지
   않습니다.

### 재접속

1. 개발용 로그인으로 영구 사용자를 복구합니다.
2. active membership 목록과 방 요약을 조회합니다.
3. 선택한 방의 최근 메시지를 제한된 page로 조회합니다.
4. `room_read_cursors`와 방의 최신 확정 위치로 읽지 않은 상태를 계산합니다.
5. 온라인 여부와 현재 열어 둔 방은 새 session 상태로 다시 구성합니다.

## 오류와 일관성 규칙

- DB 오류는 process 종료나 모든 session 종료의 근거가 아닙니다.
- 입력 오류, 권한 오류와 DB 거절은 요청 session에 명시적 오류로 반환합니다.
- handler 자체의 예상하지 못한 예외만 기존 정책에 따라 실패한 session을
  격리하며 다른 session은 유지합니다.
- 메시지 저장 전에 성공 응답이나 chat broadcast를 만들지 않습니다.
- DB timeout 뒤 결과를 추측하지 않고 멱등성 키로 재조회 또는 재시도합니다.
- adapter는 transient, constraint, timeout, unavailable 오류를 구분된 도메인
  오류로 변환합니다.
- 종료 시 새 DB 명령 수락을 멈추고 설정된 제한 시간 동안 진행 중 명령과
  완료 event를 drain합니다. 제한 시간이 끝나면 성공하지 않은 요청을
  broadcast하지 않습니다.
- 두 DB를 사용하는 미래 단계에서도 PostgreSQL과 메시지 저장소를 하나의
  분산 transaction으로 묶지 않습니다. 메시지 저장 성공을 기준 event로 삼고
  방 요약 등 파생 데이터는 멱등 projection으로 갱신합니다.

## 메시지 저장소 계약

개념적인 port는 다음 연산을 제공합니다. 실제 C++ API는 callback 또는 명시적
completion command를 사용하며 worker를 blocking하지 않습니다.

```cpp
class MessageStore {
 public:
  virtual ~MessageStore() = default;

  virtual void append(MessageDraft draft, AppendCompletion completion) = 0;
  virtual void loadRecent(RoomId room_id, PageLimit limit,
                          LoadCompletion completion) = 0;
  virtual void loadBefore(RoomId room_id, MessageId before, PageLimit limit,
                          LoadCompletion completion) = 0;
};
```

계약은 다음을 보장합니다.

- `append` 성공 결과는 영속된 확정 메시지를 포함합니다.
- 같은 사용자와 `ClientMessageId`의 반복 `append`는 같은 확정 메시지를
  반환합니다.
- page 결과는 `MessageId` 기준의 안정적인 순서를 가집니다.
- 한 page의 최대 개수와 본문 byte 합계에 상한을 둡니다.
- 구현 세부 SQL, CQL partition과 driver 타입은 port 밖에 남습니다.

## Cassandra/ScyllaDB용 데이터 형태

향후 CQL 구현의 기본 조회 단위는 `(room_id, bucket)` partition과
`message_id` clustering key입니다.

```sql
PRIMARY KEY ((room_id, bucket), message_id)
WITH CLUSTERING ORDER BY (message_id DESC)
```

`bucket`은 달력 월로 고정하지 않습니다. 방별 message 크기와 빈도 분포를
측정해 일, 주, 월 또는 크기 기반 bucket 정책을 선택합니다. 아주 큰 공개 방이
하나의 hot partition을 만들지 않게 하면서 최근 메시지 조회가 불필요하게
많은 bucket을 탐색하지 않도록 균형을 맞춥니다.

CQL 구현은 PostgreSQL과 동일한 query 계약을 지키지만 다음 차이를 adapter
내부에서 처리합니다.

- idempotency 조회를 위한 query별 비정규화 table
- 최근 page가 bucket 경계를 넘을 때의 제한된 fan-out
- consistency level과 retry 정책
- delete tombstone, compaction과 repair 운영
- room summary projection의 비동기·멱등 갱신

Cassandra는 가용성을 위해 eventual consistency를 사용하며 consistency와
availability 사이의 선택을 구성에 반영해야 합니다. 메시지 저장 성공 기준과
장애 시 동작은
[Apache Cassandra 보장 문서](https://cassandra.apache.org/doc/latest/cassandra/architecture/guarantees.html)를
기준으로 별도 검증합니다.

## 보존과 삭제

기본 정책은 명시적 삭제 전까지 메시지를 영구 보존하는 것입니다. 자동 TTL을
적용하지 않습니다.

- 일반 조회는 삭제된 메시지 본문을 반환하지 않습니다.
- 삭제 event는 기존 메시지 ID를 유지해 page 순서와 reply 참조를 깨지
  않습니다.
- 논리 삭제 후 물리 삭제 시점, 감사 기록과 backup 만료는 인증·운영 정책과
  함께 별도 결정합니다.
- 첨부 파일은 첫 단계 범위가 아니며 object storage와 metadata 경계를 별도로
  설계합니다.

## 관측성과 운영

다음 지표를 저장소 구현과 무관한 이름으로 수집합니다.

- operation별 DB 요청 수, 성공·오류·timeout 수
- DB queue 현재 크기, high watermark와 거절 수
- connection pool 사용량과 대기 시간
- message append와 page read의 p50/p95/p99 지연
- idempotent replay 횟수
- room별 message 수와 byte 분포
- shutdown 때 완료하지 못한 DB 명령 수

credential은 환경 변수 또는 배포 환경의 secret 경로로 주입하며 로그, 문서와
repository에 기록하지 않습니다. migration version을 서버 시작 시 확인하되,
운영 환경에서 임의의 destructive migration을 자동 실행하지 않습니다.

## 테스트 전략

### server-core 단위 테스트

- 고정 결과를 반환하는 in-memory repository로 영구 ID와 session 연결 검증
- 한 사용자의 여러 멤버십과 active room 분리 검증
- 저장 성공 전 chat broadcast가 없는지 검증
- 저장 성공, 중복 재시도, timeout과 명시적 실패의 출력 검증
- DB 실패가 다른 session 처리나 서버 worker 종료로 확산되지 않는지 검증
- 같은 session의 후속 명령이 미완료 명령을 추월하지 않는지 검증
- 같은 방에서 DB 완료 순서가 뒤집혀도 broadcast 순서가 유지되는지 검증
- 앞 메시지 저장이 실패한 뒤 다음 성공 메시지가 정상 게시되는지 검증
- DB queue 포화 때 bounded failure가 발생하는지 검증

### PostgreSQL integration test

- concurrent 이름 로그인에서 한 사용자만 생성되는지 검증
- 방과 owner membership transaction의 원자성 검증
- 여러 방 membership과 재접속 조회 검증
- `client_message_id` unique 충돌이 같은 확정 메시지로 수렴하는지 검증
- 최근/before page의 경계, 정렬과 최대 크기 검증
- rollback, connection 단절과 timeout 뒤 재시도 검증
- migration의 빈 DB 적용과 지원 version upgrade 검증

### 회귀와 성능 검증

- 기존 `core-dev` build와 test
- Linux 전체 server build와 integration test
- `format-check`와 가능한 경우 `tidy-check`
- PostgreSQL 지속 write, 최근 page read와 hot room 부하 시나리오
- DB 정지·재시작 중 session 격리와 복구 시나리오

## 단계적 도입

1. 영구 식별자 타입과 repository/message store port를 추가합니다.
2. PostgreSQL adapter, migration과 integration test 환경을 추가합니다.
3. 이름 기반 개발용 로그인과 session-to-user 연결을 영속화합니다.
4. 방과 다대다 membership을 영속화하고 active room을 분리합니다.
5. message append 멱등성과 저장 후 broadcast를 구현합니다.
6. 방 목록, 최근 message와 read cursor 복구를 추가합니다.
7. 장애, queue 포화와 성능 지표를 검증하고 운영 기준을 기록합니다.
8. 측정된 병목이 전환 기준을 만족하면 Cassandra와 ScyllaDB를 같은 계약으로
   비교 검증합니다.

각 단계에서 protocol을 변경하면 `docs/protocol.md`, server와 client test를
같이 수정합니다. 실행 인자나 build 절차를 변경하면 `README.md`와
`CONTRIBUTING.md`도 함께 수정합니다.

## Cassandra/ScyllaDB 전환 기준

다음 조건 중 하나가 반복 측정되고 PostgreSQL partitioning, batching,
connection 설정과 hardware 조정으로 목표를 충족하지 못할 때 전환 검토를
시작합니다.

- 목표 지속 write 처리량 또는 p99 append 지연을 충족하지 못함
- message data와 index의 보관·backup 비용이 허용 범위를 벗어남
- 단일 지역 PostgreSQL 장애 모델로 충족할 수 없는 다중 지역 write 가용성이
  필요함
- cluster 최소 구성, repair와 compaction을 담당할 운영 역량이 준비됨

전환은 application의 동시 dual write를 기본 전략으로 사용하지 않습니다.
기존 데이터를 검증 가능한 batch로 복사하고, 변경분 전달과 read 비교 기간을
거쳐 `MessageStore` binding을 전환합니다. rollback 기준과 source-of-truth
시점을 migration runbook에 명시합니다.

## 제외 범위

- 비밀번호, token, OAuth와 실제 계정 인증
- direct message의 participant uniqueness 규칙
- message 검색, attachment와 media storage
- end-to-end encryption
- 다중 지역 active-active 배포
- Cassandra 또는 ScyllaDB production cluster 구축
- 법적 보존, 감사와 물리 삭제 기간의 최종 정책

이 항목들은 현재 영구 ID와 저장소 경계를 유지한 채 후속 설계로 추가합니다.

## 완료 조건

- 영구 사용자와 session 식별자가 명확히 분리됩니다.
- 한 사용자가 여러 영구 방 membership을 가질 수 있습니다.
- message 저장 성공 전에는 성공 응답이나 broadcast가 발생하지 않습니다.
- 중복 재시도가 하나의 확정 메시지로 수렴합니다.
- DB 작업은 I/O thread와 worker를 blocking하지 않으며 queue에 상한이 있습니다.
- DB 실패는 해당 요청에 국한되고 정상 session과 server process를 유지합니다.
- PostgreSQL 구현을 바꾸지 않고도 `MessageStore` 계약 테스트를 다른 adapter에
  재사용할 수 있습니다.
- 실제 지표 없이 Cassandra 또는 ScyllaDB를 조기 도입하지 않습니다.
