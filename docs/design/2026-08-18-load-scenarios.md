# 실제 서버 부하 시나리오 설계

## 목적

단순 `PING` 왕복 측정만 제공하는 현재 부하 테스트를 보완해 방 broadcast,
다중 방 동시 처리, 느린 클라이언트 격리를 실제 TCP 연결로 반복 측정한다.
측정 실행과 서버 수명 주기를 한 명령으로 관리하고, 클라이언트가 관찰한
결과와 `TcpServer::overloadSnapshot()`의 내부 과부하 통계를 함께 기록한다.

이 도구는 절대적인 서버 최대 성능을 인증하기 위한 것이 아니라 같은 Linux
환경과 같은 실행 인자에서 서로 다른 Git commit의 처리량, 지연 시간, 실패
여부를 비교하기 위한 회귀 측정 도구다.

## 범위

### 포함

- 실제 `rss::net::TcpServer`를 loopback 임시 포트에서 별도 스레드로 실행
- 실제 POSIX TCP 소켓으로 로그인, 방 생성·참가, 채팅 broadcast 수행
- `broadcast`, `multi-room`, `slow-client` 시나리오
- warm-up 1회와 설정 가능한 측정 반복
- 전송·수신·누락·중복·연결 실패 수 집계
- 처리량과 p50/p95/p99 broadcast 지연 시간 집계
- `OverloadSnapshot`의 과부하 누적값과 최대값 기록
- Git commit, CPU, 운영체제, compiler, build type, worker 수 기록
- 사람이 읽을 수 있고 스크립트로 비교할 수 있는 `key=value` 출력
- 기존 개발 계획과 벤치마크 문서 갱신

### 제외

- 원격 서버의 내부 통계를 조회하는 관리 프로토콜
- 여러 머신을 사용하는 분산 부하 생성
- TLS, 패킷 손실 또는 WAN 지연 시뮬레이션
- 위치 업데이트 성능 시나리오
- JSON, CSV, 데이터베이스로 결과 영구 저장
- CI에서 실제 성능 임계값을 사용한 합격·실패 판정

## 접근 방식

Linux 전용 실행 파일 `rss_load_scenario_runner`를 추가한다. 실행기는 실제
`TcpServer` 인스턴스를 소유하고 `127.0.0.1:0`에서 서버를 시작한다. 포트
`0`은 운영체제가 사용 가능한 포트를 고르게 하므로 병렬 개발 환경의 포트
충돌을 피한다. 서버는 별도 스레드에서 `run()`하고, 시나리오 완료 후
`stop()`과 `join()`으로 정상 종료한다.

시나리오 클라이언트는 서버 객체를 직접 호출하지 않는다. 모든 준비와 측정
트래픽은 실제 TCP 소켓과 기존 `PacketCodec`을 사용한다. 실행기만 서버
수명 주기와 `overloadSnapshot()`에 직접 접근한다. 따라서 제품 네트워크
경로를 측정하면서도 공개 프로토콜에 관리 명령을 추가하지 않고 내부 통계를
정확히 얻을 수 있다.

기존 `rss_load_test_client`는 변경하지 않는다. 이미 실행 중인 원격 서버의
`PING`/`PONG` 지연 시간을 빠르게 확인하는 현재 역할을 유지한다.

## 구성 요소

### `ScenarioOptions`

명령행에서 받은 설정을 표현한다.

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
```

`clients`, `messages_per_sender`, `repeats`, `worker_count`는 1 이상이어야
한다. `multi-room`의 `rooms`는 1 이상이고 `clients` 이하여야 한다.
`slow-client`의 `slow_clients`는 1 이상이고 `clients`보다 작아야 한다.
`payload_bytes`는 식별자와 구분자를 포함해 64 이상 4000 이하여야 한다.
잘못된 인자는 측정을 시작하지 않고 오류 설명과 사용법을 출력한 뒤 종료
코드 `2`를 반환한다.

### `ScenarioClient`

하나의 blocking POSIX TCP 연결을 소유한다. 다음 연산을 제공한다.

- loopback 서버 연결과 종료
- 고유한 사용자 이름으로 로그인
- 방 생성과 `CREATE_ROOM_RES`에서 `room_id` 추출
- 방 참가
- 식별 가능한 채팅 메시지 전송
- `PacketCodec`으로 수신 조각을 완성 패킷으로 조립
- 기대한 broadcast를 받을 때까지 timeout을 적용해 수신

채팅 payload에는 반복, 발신 클라이언트, 메시지 순번, 발신 시각을 포함한
식별자를 사용하고 나머지를 고정 문자로 채워 `payload_bytes` 길이를 맞춘다.
수신자는 이 식별자로 누락과 중복을 구분하며, 같은 프로세스의 monotonic
clock으로 인코딩한 발신 시각을 빼서 지연 시간을 구한다. 공유 timestamp
map은 사용하지 않는다. 모든 socket 대기는 유한한 timeout을 사용해 실패한
실행이 무한정 멈추지 않게 한다.

### `ScenarioResult`

한 번의 측정 결과를 표현한다.

```cpp
struct ScenarioResult {
  std::uint64_t sent{};
  std::uint64_t expected_broadcasts{};
  std::uint64_t received_broadcasts{};
  std::uint64_t missing_broadcasts{};
  std::uint64_t duplicate_broadcasts{};
  std::uint64_t failed_clients{};
  std::vector<std::chrono::microseconds> latencies;
  rss::net::OverloadSnapshot overload;
  std::chrono::microseconds elapsed{};
};
```

`sent`는 서버에 전송을 완료한 `CHAT_REQ` 수다. `expected_broadcasts`는
각 방에서 성공적으로 전송한 메시지 수와 그 방의 읽기 참여자 수를 곱한
값이다. 송신자도 서버 broadcast 수신 대상이므로 읽기 참여자에 포함한다.

지연 시간은 발신 시각부터 각 수신자가 해당 식별자의 broadcast를 처음
수신한 시각까지로 정의한다. 중복 수신은 지연 시간 표본에 포함하지 않는다.
처리량은 `received_broadcasts / elapsed_seconds`로 계산한다.

### `EmbeddedServer`

`TcpServer`, 서버 스레드, 시작 실패 전달, 정상 종료를 묶는 작은 RAII
구성 요소다. `ServerConfig`에는 loopback 주소, 임시 포트, 선택한 worker
수를 적용한다. 시나리오가 예외로 종료돼도 destructor에서 `stop()`과
`join()`을 수행한다.

`slow-client`에서는 `max_pending_write_bytes`를 32 KiB로 낮추고 slow
client의 `SO_RCVBUF`를 1 KiB로 요청한다. fast client가 `payload_bytes`
크기의 메시지를 `messages_per_sender` 한도까지 보내는 동안 slow client
종료를 관찰한다. 한도 안에서 종료되지 않으면 성공으로 추정하지 않고 해당
반복을 실패 처리한다. 실제 적용된 socket buffer 크기는 운영체제가 조정할
수 있으므로 결과 환경 정보에 함께 출력한다.

측정 반복마다 새 `EmbeddedServer`를 만든다. 따라서 방, 세션, 누적 통계가
반복 사이에 섞이지 않는다. warm-up도 별도 서버에서 한 번 실행하고 결과는
최종 집계에 포함하지 않는다.

## 시나리오

### `broadcast`

모든 클라이언트가 한 방에 참가한다. 각 클라이언트는 정해진 수의 채팅을
보내며, 별도의 수신 작업이 모든 클라이언트에서 동시에 broadcast를 읽는다.

예상 수신 수는 다음과 같다.

```text
clients × messages_per_sender × clients
```

### `multi-room`

클라이언트를 room 번호에 round-robin으로 배치한다. 각 방의 첫 클라이언트가
방을 만들고 나머지가 참가한다. 모든 클라이언트가 채팅을 보내지만 broadcast는
같은 방의 구성원에게만 도착해야 한다.

예상 수신 수는 방별로 다음 값을 더한다.

```text
sum(room_clients × messages_per_sender × room_clients)
```

다른 방의 메시지를 받으면 예상하지 않은 broadcast로 보고 해당 반복을
실패 처리한다.

### `slow-client`

모든 클라이언트가 한 방에 참가한다. 지정한 수의 slow client는 준비가 끝난
뒤 socket 수신 버퍼를 작게 설정하고 측정 중에는 응답을 읽지 않는다. 나머지
fast client는 계속 채팅을 송수신한다.

이 시나리오의 성공 조건은 다음과 같다.

- `slow_client_disconnects`가 1 이상 증가
- fast client의 누락·중복 broadcast가 없음
- fast client 연결 실패가 없음

slow client가 읽지 않은 broadcast는 `missing_broadcasts`에 포함하지 않고
`slow_client_disconnects`로 판정한다. fast client가 받은 broadcast만 지연
시간과 처리량에 포함한다.

## 파일과 의존성 경계

플랫폼 독립 계산과 Linux 네트워크 실행을 분리한다.

```text
libs/load-test-support/
├── include/rss/tools/ScenarioOptions.h   인자 모델과 검증
├── include/rss/tools/ScenarioReport.h    결과 모델과 계산·출력 계약
├── src/ScenarioOptions.cpp               명령행 파싱
└── src/ScenarioReport.cpp                예상 수신 수와 요약 계산

apps/load-scenario-runner/
├── CMakeLists.txt
└── src/
    ├── main.cpp                          실행 조립과 종료 코드
    ├── EmbeddedServer.h/.cpp             TcpServer 수명 주기
    ├── ScenarioClient.h/.cpp             POSIX TCP 클라이언트
    └── ScenarioRunner.h/.cpp             방 준비와 시나리오 실행
```

`rss_load_test_support`는 `protocol`이나 서버 라이브러리에 의존하지 않고
순수한 설정·통계 모델만 제공한다. Linux 전용 실행기가 `rss_protocol`,
`rss_load_test_support`, `rss_server_net`에 의존하며 `OverloadSnapshot`을
플랫폼 독립 결과 모델로 복사한다. 플랫폼 독립 라이브러리가
`server-net-linux`에 의존하는 역방향은 만들지 않는다.

## 동시성

각 `ScenarioClient`는 하나의 송신 작업과 하나의 수신 작업만 사용한다.
수신 작업만 해당 클라이언트의 `PacketCodec`과 수신 집계를 변경한다.
전체 결과는 클라이언트별 결과를 thread join 이후 합산해 측정 중 공유 잠금을
피한다.

모든 클라이언트가 로그인하고 방 준비를 마친 뒤 barrier를 통과해 송신을
시작한다. 측정 시간은 barrier 해제 직전부터 기대한 broadcast를 모두 받거나
timeout이 끝날 때까지다.

## 출력과 종료 코드

실행 시작 시 환경을 한 줄로 출력하고, 각 반복 결과를 한 줄로 출력한다.

```text
environment commit=a965a8e os=Linux kernel=... cpu=... compiler=... build_type=Release workers=4
run=1 scenario=broadcast clients=100 rooms=1 sent=10000 expected=1000000 received=1000000 missing=0 duplicates=0 failed_clients=0 elapsed_sec=... throughput_broadcasts_per_sec=... p50_ms=... p95_ms=... p99_ms=... read_pauses=... inbound_queue_full=... outbound_budget_rejections=... slow_client_disconnects=... rejected_connections=... max_inbound_queue_size=... max_outbound_queue_size=... max_session_pending_write_bytes=...
```

Git commit은 빌드 시 CMake가 전달한 값이 있으면 사용하고, 없으면 `unknown`을
출력한다. CPU와 운영체제 정보는 Linux의 `/proc/cpuinfo`와 `uname`에서 읽되,
읽을 수 없는 항목만 `unknown`으로 남긴다. 결과 필드의 순서는 고정한다.

모든 반복이 시나리오 성공 조건을 만족하면 종료 코드 `0`, 측정은 끝났지만
누락·중복·클라이언트 실패 또는 slow-client 격리 실패가 있으면 `1`, 명령행
오류는 `2`, 서버 시작 또는 내부 실행 오류는 `3`을 반환한다.

## 명령행

```text
rss_load_scenario_runner \
  --scenario <broadcast|multi-room|slow-client> \
  [--clients N] [--rooms N] [--messages N] [--payload-bytes N] \
  [--slow-clients N] [--repeat N] [--workers N]
```

warm-up은 항상 측정과 같은 설정으로 1회 실행한다. 첫 버전에서는 warm-up
횟수를 별도 옵션으로 노출하지 않는다. 실행 인자를 생략하면
`ScenarioOptions`의 기본값을 사용한다.

## 테스트 전략

- 플랫폼 독립 지원 코드
  - 정상·오류 명령행 인자 파싱
  - 방별 예상 broadcast 수 계산
  - 누락·중복 계산
  - p50/p95/p99와 처리량 계산
  - 고정된 `key=value` 출력 순서
- Linux 네트워크 통합 테스트
  - 작은 `broadcast` 시나리오가 누락 없이 완료
  - 두 방의 메시지가 서로 섞이지 않음
  - 작은 pending write 한도에서 slow client만 종료
  - server 시작 실패와 timeout이 비정상 종료 코드로 변환
- 회귀 검증
  - 기존 `rss_load_test_client` 동작과 출력 유지
  - `linux-dev` build/test, `format-check`, 가능한 경우 `tidy-check`

성능 통합 테스트는 작은 클라이언트와 메시지 수만 사용하고 절대 처리량을
assert하지 않는다. CI 환경 차이로 흔들리지 않도록 정확성, 종료 여부, 통계
증가만 검증한다.

## 문서 변경

- `docs/development-plan.md`의 과부하 제어를 완료 상태로 수정
- 실제 서버 시나리오 측정은 구현 완료 시 완료 상태로 수정
- `docs/benchmark.md`에 새 실행 파일, 시나리오, 출력, 비교 절차 추가
- `README.md`의 부하 테스트 절에 원격 PING 도구와 로컬 시나리오 실행기의
  차이를 설명

## 제약과 해석

서버와 부하 클라이언트가 같은 프로세스와 머신의 CPU 및 메모리를 공유한다.
따라서 결과는 분산 부하 생성기의 최대 처리량과 같지 않다. 서로 다른 commit을
비교할 때는 같은 하드웨어, 같은 build type, 같은 worker 수, 같은 실행 인자를
사용해야 한다.

`slow-client` 결과는 운영체제 socket buffer 크기와 scheduling에 영향을
받는다. 테스트는 절대 시간을 성능 기준으로 사용하지 않고, 제한된 서버
설정에서 slow client 종료 통계와 fast client 무손실 여부를 확인한다.
