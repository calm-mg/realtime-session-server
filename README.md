# realtime-session-server

Linux 환경에서 동작하는 C++ 기반 실시간 세션 서버입니다.
non-blocking TCP, epoll 이벤트 루프, 바이너리 패킷 처리, 세션 생명주기 관리, 룸 기반 메시지 라우팅, 멀티스레드 작업 처리를 중심으로 구현했습니다.

This project implements a Linux-based real-time session server in modern C++.
It focuses on non-blocking TCP networking, epoll-based event handling, binary packet processing, session lifecycle management, room-based message routing, and concurrent worker execution.

## Features

- TCP server with non-blocking sockets
- epoll-based event loop
- session accept/read/write lifecycle management
- heartbeat-style idle timeout and `PING`/`PONG`
- binary packet header: `uint16_t size`, `uint16_t type`
- login, room create/join/leave, room chat, position broadcast
- main I/O thread plus worker threads
- thread-safe inbound/outbound queues
- console client and simple load test client
- dependency-free unit tests

## Build

The networking targets require Linux because the server uses `epoll`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

On non-Linux platforms, the core protocol/domain/service tests can still be configured, but `rss_server`, `rss_console_client`, and `rss_load_test_client` are disabled.

## Run

```bash
./build/rss_server 0.0.0.0 7777 4
```

In another terminal:

```bash
./build/rss_console_client 127.0.0.1 7777
```

Client commands:

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

Load test:

```bash
./build/rss_load_test_client 127.0.0.1 7777 1000 100
```

## Layout

```text
include/rss/net        epoll loop, TCP server, session, worker pool
include/rss/protocol   packet type and binary codec
include/rss/domain     user, room, lobby models
include/rss/service    room service and message router
src                    implementation
client                 interactive console client
tools                  load test client
test                   dependency-free tests
docs                   architecture, protocol, benchmark notes
```

## Portfolio Points

- The I/O thread only owns sockets and epoll interest changes.
- Worker threads decode commands through a thread-safe queue and return outbound packets through another queue.
- Domain logic is isolated from Linux networking code, so protocol/service tests run without sockets.
- The protocol is intentionally small and binary-framed, which makes partial reads, packet coalescing, and malformed packet handling visible in code.
