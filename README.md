# realtime-session-server

C++20로 구현한 Linux 기반 실시간 룸 서버입니다.

실시간 게임/채팅 서버에서 자주 등장하는 세션 관리, 룸 기반 메시지 라우팅, 바이너리 패킷 프레이밍, non-blocking TCP, `epoll` 이벤트 루프, worker thread 기반 처리를 직접 구현했습니다.

이 프로젝트의 목표는 단순히 기능을 붙이는 것이 아니라, **서버 구조와 성능 개선 과정을 포트폴리오로 보여주는 것**입니다. 현재 구현은 baseline 역할을 하며, 이후 benchmark를 통해 p50/p95/p99 latency, broadcast fanout, backpressure, queue 병목을 측정하고 개선하는 방향으로 확장합니다.

## 주요 기능

- non-blocking TCP 서버
- Linux `epoll` 기반 이벤트 루프
- 세션 accept/read/write 생명주기 관리
- idle timeout 및 `PING`/`PONG`
- `uint16_t size`, `uint16_t type` 기반 바이너리 패킷 헤더
- 로그인, 룸 생성/입장/퇴장, 룸 채팅, 위치 브로드캐스트
- I/O thread와 worker thread 분리
- thread-safe inbound/outbound queue
- worker completion을 I/O thread에 전달하는 `eventfd` wakeup
- 콘솔 클라이언트와 `PING`/`PONG` latency load test client
- 외부 테스트 프레임워크 없이 실행 가능한 core unit test

## 빌드

서버와 클라이언트 타깃은 `epoll`을 사용하므로 Linux에서 빌드됩니다.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

macOS 같은 non-Linux 환경에서는 network target인 `rss_server`, `rss_console_client`, `rss_load_test_client`가 비활성화됩니다. 대신 protocol/domain/service 계층의 core test는 계속 빌드하고 실행할 수 있습니다.

## 실행

```bash
./build/rss_server 0.0.0.0 7777 4
```

다른 터미널에서 콘솔 클라이언트를 실행합니다.

```bash
./build/rss_console_client 127.0.0.1 7777
```

클라이언트 명령어:

```text
/login alice
/create arena
/join 1
/chat hello
/pos 10.5 22.0
/ping
/leave
/quit
```

`PING`/`PONG` latency load test:

```bash
./build/rss_load_test_client 127.0.0.1 7777 1000 100
```

## 구조

```text
include/rss/net        epoll loop, TCP server, session, worker pool
include/rss/protocol   packet type, binary codec
include/rss/domain     user, room, lobby model
include/rss/service    room service, message router
src                    구현 코드
client                 대화형 콘솔 클라이언트
tools                  load test client
test                   외부 프레임워크 없는 unit test
docs                   아키텍처, 프로토콜, 벤치마크 문서
```

## 포트폴리오 포인트

- I/O thread가 socket과 `epoll_ctl` ownership을 가진다.
- worker thread는 socket API를 직접 호출하지 않고, queue를 통해 command를 처리한다.
- worker 결과는 `eventfd` wakeup으로 I/O thread에 전달된다.
- domain/service 로직을 Linux networking 코드와 분리해 socket 없이도 core test를 실행할 수 있다.
- 작은 binary protocol을 직접 구현해 TCP partial read, packet coalescing, malformed packet 처리를 코드로 보여준다.
- 이후 개선 방향은 bounded queue, backpressure, room shard/actor 모델, p99 latency 측정이다.

## 향후 개선 방향

현재 구현은 baseline 서버로 유지하고, 다음 순서로 성능 포트폴리오를 강화합니다.

1. benchmark client를 확장해 room broadcast fanout, slow client 영향, queue saturation을 측정한다.
2. unbounded queue와 session write buffer에 capacity를 두고 overload/backpressure 동작을 명확히 한다.
3. 전역 mutex 기반 `RoomService`를 room shard/actor-style ownership 구조로 개선한다.
4. baseline과 개선 버전을 같은 환경에서 비교해 `docs/benchmark.md`에 결과와 해석을 기록한다.

## 오픈소스 사용 방향

네트워크 핵심부는 `epoll`과 `eventfd`를 직접 다루는 구조를 유지합니다. 대신 테스트, 벤치마크, 로그처럼 서버의 핵심 구현을 흐리지 않는 영역에는 검증된 오픈소스를 선택적으로 도입합니다.

- GoogleTest: 단위 테스트와 shard/queue 동작 검증
- Google Benchmark: codec, queue, broadcast fanout microbenchmark
- `fmt`/`spdlog`: benchmark 결과와 server log 출력 정리
