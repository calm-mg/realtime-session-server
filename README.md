# realtime-session-server

C++20과 Linux `epoll`로 만든 실시간 세션 서버입니다.

여러 클라이언트가 TCP로 서버에 접속한 뒤 로그인하고, 방을 만들거나
참가해서 채팅과 위치 정보를 주고받을 수 있습니다. 서버는 한 스레드에서
네트워크 입출력을 처리하고, 별도의 worker 스레드에서 명령을 처리합니다.

## 주요 기능

- non-blocking TCP 연결
- Linux `epoll` 기반 이벤트 처리
- 로그인과 접속 종료 처리
- 방 생성, 참가, 나가기
- 같은 방에 있는 사용자에게 채팅과 위치 정보 전송
- `PING`/`PONG` 연결 확인
- 4바이트 헤더를 사용하는 바이너리 패킷
- I/O 스레드와 worker 스레드 분리
- `eventfd`를 사용한 worker 완료 알림
- 콘솔 클라이언트와 `PING` 부하 테스트 도구
- 프로토콜, 서비스, 네트워크 구성 요소 테스트

## 필요한 환경

전체 서버는 `epoll`과 `eventfd`를 사용하므로 Linux에서 빌드해야 합니다.
Windows에서는 WSL2의 Ubuntu를 사용할 수 있습니다.

- C++20을 지원하는 GCC 또는 Clang
- CMake 3.20 이상
- Ninja 또는 Make
- POSIX Threads

Ubuntu에서는 다음 패키지로 시작할 수 있습니다.

```bash
sudo apt-get update
sudo apt-get install --yes build-essential cmake ninja-build
```

## 빠른 시작

### 1. 빌드

저장소 루트에서 다음 명령을 실행합니다.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### 2. 테스트

```bash
ctest --test-dir build --output-on-failure
```

### 3. 서버 실행

```bash
./build/rss_server 0.0.0.0 7777 4
```

인자는 순서대로 다음 의미입니다.

```text
rss_server <접속을 받을 주소> <포트> <worker 스레드 수>
```

인자를 생략하면 주소는 `0.0.0.0`, 포트는 `7777`을 사용합니다. worker
수는 명시하지 않으면 CPU가 제공하는 동시 실행 수를 기준으로 정합니다.

### 4. 클라이언트 접속

서버를 실행한 상태에서 새 터미널을 열고 다음 명령을 실행합니다.

```bash
./build/rss_console_client 127.0.0.1 7777
```

아래 순서대로 입력하면 로그인, 방 생성, 채팅을 시험할 수 있습니다.

```text
/login alice
/create arena
/chat hello
/pos 10.5 22.0
/ping
/leave
/quit
```

두 번째 클라이언트로 같은 방에 들어가려면 서버가 반환한 방 번호를
사용합니다.

```text
/login bob
/join 1
/chat 반갑습니다
```

| 명령 | 설명 |
| --- | --- |
| `/login <이름>` | 사용자 이름으로 로그인 |
| `/create <방 이름>` | 새 방을 만들고 입장 |
| `/join <방 번호>` | 기존 방에 입장 |
| `/leave` | 현재 방에서 나가기 |
| `/chat <메시지>` | 현재 방에 채팅 전송 |
| `/pos <x> <y>` | 현재 방에 위치 좌표 전송 |
| `/ping` | 서버의 `PONG` 응답 확인 |
| `/quit` | 클라이언트 종료 |

`/`로 시작하지 않는 일반 문장도 채팅 메시지로 전송됩니다.

## 간단한 부하 테스트

다음 명령은 클라이언트 100개가 각각 `PING`을 100번 보내고 `PONG`
응답 시간을 측정합니다.

```bash
./build/rss_load_test_client 127.0.0.1 7777 100 100
```

출력에는 전송 수, 실패한 클라이언트 수, 초당 처리량과
`p50`/`p95`/`p99` 응답 시간이 포함됩니다. 자세한 측정 방법은
[벤치마크 가이드](docs/benchmark.md)를 참고하세요.

## 서버가 요청을 처리하는 순서

1. I/O 스레드가 `epoll`로 소켓 이벤트를 기다립니다.
2. 소켓에서 읽은 바이트를 `PacketCodec`이 완전한 패킷으로 나눕니다.
3. 완성된 패킷을 작업 큐에 넣습니다.
4. worker 스레드가 로그인, 방, 채팅 같은 명령을 처리합니다.
5. worker가 응답을 출력 큐에 넣고 `eventfd`로 I/O 스레드를 깨웁니다.
6. I/O 스레드가 응답을 각 클라이언트 소켓으로 전송합니다.

소켓의 읽기, 쓰기, 연결 종료와 `epoll_ctl` 호출은 I/O 스레드만
담당합니다. worker 스레드는 소켓을 직접 조작하지 않습니다.

## 디렉터리 구조

```text
include/rss/net/       소켓, epoll, 세션, worker 인터페이스
include/rss/protocol/  패킷 종류와 인코딩 규칙
include/rss/domain/    사용자, 방, 로비 데이터
include/rss/service/   로그인, 방, 메시지 처리
src/                   라이브러리와 서버 구현
client/                대화형 콘솔 클라이언트
tools/                 PING 부하 테스트 클라이언트
test/                  자동 테스트
docs/                  구조, 프로토콜, 벤치마크 설명
```

## 현재 제약 사항

- 네트워크 서버와 클라이언트는 Linux에서만 빌드됩니다.
- 작업 큐와 세션별 전송 대기열에 크기 제한이 없습니다.
- 방과 사용자 상태는 하나의 `RoomService` mutex로 보호합니다.
- 부하 테스트 도구는 현재 `PING`/`PONG` 시나리오만 지원합니다.
- TLS, 인증 토큰, 데이터 영속 저장은 구현되어 있지 않습니다.

## 더 자세한 문서

- [서버 구조](docs/architecture.md)
- [패킷 프로토콜](docs/protocol.md)
- [벤치마크 실행과 해석](docs/benchmark.md)
- [개발 및 코드 스타일](CONTRIBUTING.md)
