# 서버 구조

이 문서는 클라이언트 요청이 서버에 들어와 응답으로 나가기까지의 흐름과
과부하 시 서버가 적용하는 상한을 설명합니다.

## 먼저 알아둘 용어

- **세션(session)**: 클라이언트 TCP 연결 하나를 나타내는 객체
- **I/O 스레드**: 소켓 연결, 읽기, 쓰기와 `epoll` 관심 이벤트를 담당하는
  스레드
- **worker 스레드**: 로그인, 방, 채팅 같은 애플리케이션 명령을 처리하는
  스레드
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

### `WorkerPool`과 `SessionEventHandler`

`WorkerPool`은 구체적인 메시지 라우터가 아니라 애플리케이션 처리 포트인
`SessionEventHandler`에만 의존합니다. `MessageRouter`가 이 포트를
구현하고 `RoomService`와 협력해 `SessionEvent`를 `OutboundMessage` 목록으로
변환합니다. worker는 소켓이나 `epoll`을 직접 조작하지 않습니다.

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

## 출력 흐름과 느린 클라이언트 격리

출력 queue가 포화되면 worker의 출력 생산이 제한됩니다. worker 처리량이
낮아지면 입력 queue가 high watermark에 도달해 앞에서 설명한 읽기
일시정지로 압력이 역전파됩니다. 이 동안에도 I/O 스레드는 socket write,
notifier, 종료 이벤트를 계속 처리합니다.

각 세션은 `max_pending_write_bytes`를 넘지 않는 미전송 응답만 보관합니다.
한도를 넘기는 출력은 그 세션의 연결만 종료하며, 같은 출력 batch의 다른
세션 메시지는 계속 처리합니다. 최대 세션 수에 도달한 경우에는 새로
accept된 연결만 즉시 닫고 기존 세션은 유지합니다.

## 설정 기본값

| 설정 | 기본값 | 의미 |
| --- | ---: | --- |
| `inbound_queue_capacity` | 4096 | 입력 이벤트 최대 개수 |
| `inbound_high_watermark` | 3072 | 읽기·accept 일시정지 기준 |
| `inbound_low_watermark` | 2048 | 읽기·accept 재개 기준 |
| `outbound_queue_capacity` | 4096 | 출력 메시지 최대 개수 |
| `max_pending_write_bytes` | 1 MiB | 세션 하나의 미전송 바이트 상한 |
| `max_sessions` | 10000 | 동시에 보유하는 세션 상한 |
| `graceful_shutdown_timeout` | 5초 | 정상 종료 시 drain 최대 대기 시간 |

`ServerConfig::validate()`는 0 값, watermark 순서 위반, capacity 초과와
유효하지 않은 종료 시간을 서버 시작 전에 거절합니다.

## 과부하 통계

`TcpServer::overloadSnapshot()`은 읽기 일시정지·재개, 입력 queue 포화,
느린 클라이언트 종료, 연결 거절 횟수와 관측한 최대 queue·세션 pending
write 크기를 제공합니다. 현재 입력·출력 queue 크기와 세션 수도 함께
제공합니다.

snapshot의 각 항목은 동시 갱신 중에도 독립적으로 읽을 수 있지만, 여러
필드가 하나의 동일한 시점을 나타내도록 묶이지는 않습니다. 따라서 단일
필드의 관측과 추세 확인에는 사용할 수 있으나, 서로 다른 필드의 조합을
하나의 원자적 상태로 해석하지 않아야 합니다.

## 종료 순서

종료 요청을 받으면 서버는 새 연결 수락과 새 소켓 읽기를 멈춘 뒤, 이미
디코딩된 패킷과 지연된 종료 이벤트를 입력 queue에 전달합니다. 이어서
worker가 만든 출력과 세션의 pending write를 설정된 시간 동안 drain합니다.
제한 시간에 도달하면 두 queue를 닫아 대기 중인 worker를 깨우고, 남은
네트워크 자원을 정리합니다.

## 스레드 사용 규칙

- I/O 스레드만 소켓과 `epoll` 상태를 변경합니다.
- worker 스레드는 `SessionEventHandler`와 thread-safe queue로만 I/O
  스레드와 통신합니다.
- 방과 사용자 상태는 `RoomService`의 mutex로 보호합니다.
- 통계 수집은 파일 기록이나 외부 수집기를 기다리지 않습니다.
