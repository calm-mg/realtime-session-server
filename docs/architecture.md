# 서버 구조

이 문서는 클라이언트 요청이 서버에 들어와 응답으로 나가기까지의 흐름과
과부하 시 서버가 적용하는 상한을 설명합니다.

## 먼저 알아둘 용어

- **세션(session)**: 클라이언트 TCP 연결 하나를 나타내는 객체
- **I/O 스레드**: 소켓 연결, 읽기, 쓰기와 `epoll` 관심 이벤트를 담당하는
  스레드
- **worker 스레드**: 로그인, 방, 채팅 같은 애플리케이션 명령을 처리하는
  스레드
- **DB executor 스레드**: 전용 PostgreSQL 연결에서 blocking `libpq` 작업을
  실행하는 스레드
- **queue**: 한 스레드가 다른 스레드에 작업을 넘길 때 사용하는 대기열
- **high/low watermark**: 읽기를 멈추고 다시 시작하는 입력 queue 크기 기준
- **pending write**: 세션이 아직 소켓으로 전부 보내지 못한 응답 바이트

## 전체 흐름

```text
클라이언트
    │ TCP 요청
    ▼
I/O 스레드
    │ epoll로 이벤트 확인, 바이트를 패킷으로 변환
    ▼
용량 제한 입력 queue
    ▼
WorkerPool
    │ SessionEventHandler로 명령 처리
    ├── 로그인: 용량 제한 DB queue → PostgreSQL → deferred completion
    │
    ▼
용량 제한 출력 queue
    │ eventfd 완료 알림
    ▼
I/O 스레드
    │ 세션별 송신 상한을 확인해 소켓으로 전송
    ▼
클라이언트
```

## 구성 요소

### `TcpServer`

서버의 중심 객체입니다. listener와 세션 소켓을 `epoll`로 감시하고,
입력·출력 queue, 읽기 흐름 제어, 세션 수 제한, 정상 종료를 관리합니다.
`accept4`, `recv`, `send`, `epoll_ctl`, `close`는 I/O 스레드만 호출합니다.
`epoll` 이벤트에는 fd 대신 등록할 때마다 달라지는 식별자를 저장합니다.
이미 반환된 이벤트 묶음에 닫힌 연결의 이벤트가 남아 있어도, 같은 fd를
재사용한 새 세션과 식별자가 다르면 해당 이벤트를 무시합니다.

### `WorkerPool`과 `SessionEventHandler`

`WorkerPool`은 구체적인 메시지 라우터가 아니라 애플리케이션 처리 포트인
`SessionEventHandler`에만 의존합니다. `MessageRouter`가 이 포트를
구현하고 `RoomService`와 협력해 응답을 출력 sink로 하나씩 전달합니다.
worker는 응답 목록 전체를 로컬에 쌓지 않으며 소켓이나 `epoll`을 직접
조작하지 않습니다.

각 `SessionEvent`에는 세션 안에서 증가하는 순서 번호가 있습니다. 여러
worker가 동시에 동작해도 같은 세션의 이벤트는 이 번호 순서대로 하나씩
처리합니다. 따라서 마지막 패킷 처리보다 `Disconnected`가 먼저 실행되어
종료한 사용자가 다시 생성되는 순서 역전을 막습니다. 서로 다른 세션은
계속 병렬로 처리할 수 있습니다.

handler가 DB 작업을 시작하면 현재 이벤트를 deferred 상태로 전환합니다.
worker는 DB 응답을 기다리지 않고 다른 세션을 처리하며, 같은 세션의 후속
이벤트는 `max_parked_events_per_session`까지 순서 번호별로 보관합니다. DB
완료 이벤트가 돌아오면 원래 이벤트의 응답을 먼저 게시한 뒤 보관한 다음
이벤트를 처리합니다. 상한을 넘긴 세션이나 실패한 DB completion만 연결 종료
대상이 되며 다른 세션과 서버 프로세스는 유지합니다.

### PostgreSQL 사용자 저장소

운영 `rss_server`는 이름 조회 키와 영구 UUID를 PostgreSQL `users` table에
저장합니다. `MessageRouter`는 DB 중립적인 `UserRepository`에만 의존하고,
parameterized SQL과 `PGconn`은 별도 PostgreSQL adapter 내부에 남습니다.
DB executor는 worker마다 전용 연결 하나를 사용하며 queue 용량을 제한합니다.
queue 포화, timeout, 연결 장애와 종료 상태는 안정된 저장소 오류로 변환되고
원본 연결 문자열이나 DB 오류 상세는 protocol payload에 포함하지 않습니다.

기본 embedded `TcpServer`는 외부 DB 없이 시험할 수 있도록 in-memory 사용자
저장소를 사용합니다. 방, 참가 상태, 채팅과 읽음 위치는 아직 영속화하지
않습니다.

### `BoundedBlockingQueue`

입력과 출력 queue는 모두 용량을 가집니다. 입력 전달은 기다리지 않는
`tryPush`로 수행하고, 출력 queue가 가득 차면 worker만 중단 가능한 `push`로
공간 또는 queue 종료를 기다립니다. 따라서 I/O 이벤트 루프는 queue 공간이나
worker 종료를 기다리며 멈추지 않습니다.

### `Session`과 `PacketCodec`

`Session`은 TCP 연결, 패킷 codec, 마지막 수신 시각과 미전송 응답을
보관합니다. `PacketCodec`은 다음 완성 패킷을 먼저 확인하고, 입력 queue
삽입이 성공한 뒤에만 해당 패킷을 소비합니다. 입력 queue가 가득 차도 이미
디코딩한 패킷은 codec에 남아 순서와 내용을 보존합니다.

codec은 패킷 하나를 소비할 때마다 앞부분을 이동하지 않고 읽기 위치를
전진시킵니다. 소비한 영역이 일정 크기 이상 쌓였을 때만 한 번에 압축해,
작은 패킷을 연속으로 처리할 때 반복적인 전체 버퍼 이동을 피합니다.

## 입력 흐름 제어

입력 queue 설정은 다음 불변식을 만족해야 합니다.

```text
0 < inbound_low_watermark < inbound_high_watermark <= inbound_queue_capacity
```

입력 queue 크기가 high watermark에 도달하면 서버는 listener를 감시 대상에서
제거하고 모든 세션의 `EPOLLIN`을 해제합니다. 이때 새 연결 수락과 추가
소켓 읽기가 일시정지되며, 이미 연결된 세션은 전역 과부하만으로 종료하지
않습니다.

worker가 입력 queue에서 이벤트를 꺼낸 뒤 low watermark를 관측하면 입력
공간 알림을 보냅니다. I/O 스레드는 이 알림을 처리하면서 codec에 보류된
패킷과 지연된 종료 이벤트를 먼저 입력 queue에 전달하고, 여유가 유지되면
listener와 세션의 `EPOLLIN`을 다시 등록합니다.

입력 queue가 포화된 상태에서 세션이 종료되면 `Disconnected` 이벤트를
버리지 않습니다. 해당 세션은 최대 `max_sessions`개까지 보관하는 지연 목록에
넣고, queue가 회복된 뒤 보류 패킷과 종료 이벤트를 전달합니다.

읽기가 일시정지된 동안 상대가 정상적으로 송신을 종료하면 서버는 종료
상태만 먼저 기록합니다. 입력 queue에 여유가 생긴 뒤 소켓 수신 버퍼를
non-blocking 방식으로 EOF까지 읽고, 그 안의 완성 패킷을 모두 전달한 다음
`Disconnected`를 전달합니다.

## 출력 흐름과 느린 클라이언트 격리

출력 queue가 포화되면 worker의 출력 생산이 제한됩니다. worker 처리량이
낮아지면 입력 queue가 high watermark에 도달해 앞에서 설명한 읽기
일시정지로 압력이 역전파됩니다. 이 동안에도 I/O 스레드는 socket write,
notifier, 종료 이벤트를 계속 처리합니다.

한 입력 이벤트가 만들 수 있는 출력 메시지 수와 총 바이트도
`max_outbound_messages_per_event`,
`max_outbound_bytes_per_event`로 제한합니다. 출력 sink는 한도를 넘는
메시지와 크기가 0인 메시지를 queue에 넣지 않습니다. 기본 메시지 수 한도는
기본 최대 세션 수와 같아, 내장 방 broadcast가 정상 범위에서 잘리지 않도록
설정되어 있습니다.

각 세션은 `max_pending_write_bytes`를 넘지 않는 미전송 응답만 보관합니다.
한도를 넘기는 출력은 그 세션의 연결만 종료하며, 같은 출력 batch의 다른
세션 메시지는 계속 처리합니다. 최대 세션 수에 도달한 경우에는 새로
accept된 연결만 즉시 닫고 기존 세션은 유지합니다.

## 설정 기본값과 유효 범위

| 설정 | 기본값 | 유효 범위 | 의미 |
| --- | ---: | --- | --- |
| `host` | `0.0.0.0` | 비어 있지 않은 문자열 | IPv4 listener bind 주소 |
| `port` | 7777 | 0..65535 | listener port. 0은 운영체제가 선택하는 ephemeral port |
| `worker_count` | 4 | 1 이상 | worker 스레드 수 |
| `backlog` | 512 | 1 이상 | listener 연결 대기열 크기 |
| `max_events` | 256 | 1 이상 | 한 번의 `epoll_wait()`에서 받을 최대 이벤트 수 |
| `idle_timeout` | 60초 | 0초 초과 | 유휴 세션 종료 기준 |
| `inbound_queue_capacity` | 4096 | 1 이상 | 입력 이벤트 최대 개수 |
| `inbound_high_watermark` | 3072 | low 초과, capacity 이하 | 읽기·accept 일시정지 기준 |
| `inbound_low_watermark` | 2048 | 0 초과, high 미만 | 읽기·accept 재개 기준 |
| `outbound_queue_capacity` | 4096 | 1 이상 | 출력 메시지 최대 개수 |
| `max_outbound_messages_per_event` | 10000 | 1 이상 | 입력 이벤트 하나의 출력 메시지 상한 |
| `max_outbound_bytes_per_event` | 40 MiB | 1 byte 이상 | 입력 이벤트 하나의 출력 총 바이트 상한 |
| `max_pending_write_bytes` | 1 MiB | 1 byte 이상 | 세션 하나의 미전송 바이트 상한 |
| `max_sessions` | 10000 | 1 이상 | 동시에 보유하는 세션 상한 |
| `max_parked_events_per_session` | 32 | 1 이상 | deferred 처리 중 세션별 보관 이벤트 상한 |
| `graceful_shutdown_timeout` | 5초 | 0초 초과 | 정상 종료 시 drain 최대 대기 시간 |
| `emit_startup_diagnostic` | `true` | `true` 또는 `false` | 시작 진단 메시지 출력 여부 |

`ServerConfig::validate()`는 빈 host, 0 이하의 worker·listener·event loop
설정, queue와 출력 상한의 0 값, watermark 순서 위반, capacity 초과와
유효하지 않은 timeout을 서버 시작 전에 거절합니다. IPv4 문자열 형식은
Linux 네트워크 계층이 listener를 열 때 검사합니다.

## handler 예외 격리

`SessionEventHandler::handle()`에서 예외가 빠져나오면 worker는 서버 전체를
종료하지 않고 해당 세션을 실패 상태로 격리합니다. 이미 입력 queue에 들어온
후속 패킷은 handler에 전달하지 않되 세션 순서 번호는 계속 진행합니다.
이벤트 하나에서 만든 출력은 handler가 정상 반환할 때까지 worker에 보관하므로
예외 전에 만든 부분 응답이나 broadcast도 게시되지 않습니다. worker는 출력
queue에 `DisconnectSession` 제어 명령을 넣고, I/O 스레드가 소켓을 닫습니다.
worker는 소켓을 직접 조작하지 않습니다.

실패한 세션의 `Disconnected` 이벤트는 격리 중에도 handler에 전달합니다.
따라서 기본 `MessageRouter`는 `RoomService`의 사용자와 방 참가 상태를 기존
연결 종료 경로로 정리할 수 있습니다. handler 예외 횟수는
`handler_exceptions` 통계에 누적합니다.

## 과부하 통계

`TcpServer::overloadSnapshot()`은 읽기 일시정지·재개, 입력 queue 포화,
출력 예산 거절, handler 예외, 느린 클라이언트 종료, 연결 거절 횟수와
관측한 최대 queue·세션 pending write 크기를 제공합니다. 현재 입력·출력
queue 크기, 출력 queue에서 공간을 기다리는 worker 수와 세션 수도 함께
제공합니다.

snapshot의 각 항목은 동시 갱신 중에도 독립적으로 읽을 수 있지만, 여러
필드가 하나의 동일한 시점을 나타내도록 묶이지는 않습니다. 따라서 단일
필드의 관측과 추세 확인에는 사용할 수 있으나, 서로 다른 필드의 조합을
하나의 원자적 상태로 해석하지 않아야 합니다.

## 운영 로그와 통계 노출

운영 서버는 외부 logging library나 별도 관리 port 없이 NDJSON을 표준
스트림에 기록합니다. 컨테이너와 process supervisor가 그대로 수집할 수 있고,
로그 구현 때문에 I/O 스레드가 외부 network 호출을 수행하지 않습니다.

- `server_started`: listener가 열린 뒤 host, 실제 port와 worker 수를 기록
- `overload_snapshot`: 기본 30초마다 현재 `OverloadSnapshot` 전체를 기록
- `overload_snapshot`의 `final` phase: network drain 완료 뒤 최종값 기록
- `server_stopped`: 최종 통계 다음에 정상 종료 완료를 기록
- `server_failed`: 시작 또는 실행 실패 원인을 표준 오류에 기록

모든 이벤트에는 Unix epoch 밀리초 `timestamp_unix_ms`, `level`, `event`가
있습니다. 정보 이벤트는 표준 출력, 실패 이벤트는 표준 오류를 사용합니다.
`RSS_OBSERVABILITY_INTERVAL_SECONDS`는 0 이상의 정수이며 기본값은 30입니다.
0은 주기 snapshot만 끕니다. 최종 snapshot은 정상 종료 시 항상 기록합니다.

주기 reporter는 `TcpServer::overloadSnapshot()` provider에만 의존하는
플랫폼 독립 구성 요소입니다. 자체 thread는 설정 주기마다 snapshot을 읽으며,
종료 요청은 condition variable로 대기 시간을 즉시 중단합니다. 출력 실패나
snapshot 수집 실패가 서버를 종료시키지 않도록 reporter thread 밖으로 예외를
전파하지 않습니다.

운영 로그에는 사용자 이름, 방 이름, 채팅 payload, 인증정보와 database URL을
넣지 않습니다. 여러 snapshot 필드의 원자성 제약은 위 과부하 통계 설명과
동일합니다.

## 종료 순서

종료 요청을 받으면 서버는 새 연결 수락과 새 소켓 읽기를 멈춘 뒤, 이미
디코딩된 패킷과 지연된 종료 이벤트를 입력 queue에 전달합니다. 이어서
worker가 만든 출력과 세션의 pending write를 설정된 시간 동안 drain합니다.
한 번의 I/O 순환에서 처리하는 지연 입력, 출력 메시지와 socket write
바이트에는 작업 예산이 있어 종료 시각을 주기적으로 다시 확인합니다.
제한 시간에 도달하면 두 queue를 닫아 대기 중인 worker를 깨웁니다. 현재
실행 중인 handler가 반환하면 아직 queue에 남은 이벤트는 더 처리하지 않고
네트워크 자원을 정리합니다.

운영 실행 파일은 네트워크 drain이 끝날 때까지 PostgreSQL executor를 유지한
뒤 새 DB 작업 수락을 중단하고 이미 접수한 작업을 join합니다. 그동안 router,
방 서비스와 completion queue도 살아 있어 늦게 끝난 callback이 파괴된 객체에
접근하지 않습니다. DB statement에는 timeout을 적용해 종료 대기가 무한히
늘어나지 않도록 합니다.

`TcpServer::stop()`이 호출되면 이 종료 절차가 시작됩니다. `rss_server` 실행
파일은 worker를 만들기 전에 `SIGINT`와 `SIGTERM`을 차단하고 전용
`sigwait()` 스레드에서 두 신호를 기다립니다. 신호를 받으면 이 스레드는
`stop()`만 호출하며, `eventfd` 알림을 받은 I/O 스레드가 기존 drain 상태
머신을 진행합니다.

프로세스 신호 정책은 서버 애플리케이션에만 있습니다. 부하 시나리오의
embedded server와 `TcpServer`를 직접 사용하는 코드는 기존처럼 `stop()`을
명시적으로 호출합니다.

## 스레드 사용 규칙

- I/O 스레드만 소켓과 `epoll` 상태를 변경합니다.
- worker 스레드는 `SessionEventHandler`와 thread-safe queue로만 I/O
  스레드와 통신합니다.
- 방과 사용자 상태는 `RoomService`의 mutex로 보호합니다.
- 통계 수집은 파일 기록이나 외부 수집기를 기다리지 않습니다.
