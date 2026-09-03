# realtime-session-server

C++20과 Linux `epoll`로 만든 실시간 세션 서버입니다.

여러 클라이언트가 TCP로 서버에 접속한 뒤 로그인하고, 방을 만들거나
참가해서 채팅과 위치 정보를 주고받을 수 있습니다. 서버는 한 스레드에서
네트워크 입출력을 처리하고, 별도의 worker 스레드에서 명령을 처리합니다.

## 주요 기능

- non-blocking TCP 연결
- Linux `epoll` 기반 이벤트 처리
- 로그인과 접속 종료 처리
- PostgreSQL에 저장하는 영구 사용자 ID와 재접속 복구
- 방 생성, 참가, 나가기
- 같은 방에 있는 사용자에게 채팅과 위치 정보 전송
- `PING`/`PONG` 연결 확인
- 4바이트 헤더를 사용하는 바이너리 패킷
- I/O 스레드와 worker 스레드 분리
- `eventfd`를 사용한 worker 완료 알림
- 설정 가능한 입력·출력 queue, 세션별 송신 대기 byte, 동시 세션 상한
- 같은 세션의 패킷과 연결 종료 이벤트 순서 보장
- 입력 이벤트별 출력 메시지 수와 총 byte 상한
- 콘솔 클라이언트와 두 종류의 부하 측정 도구
- Qt 6 Widgets 기반 크로스플랫폼 데스크톱 클라이언트
- GoogleTest 기반 프로토콜, 서비스, 네트워크 구성 요소 테스트
- Google Benchmark 기반 핵심 코드 경로 마이크로벤치마크

## 필요한 환경

전체 서버는 `epoll`과 `eventfd`를 사용하므로 Linux에서 빌드해야 합니다.
Windows에서는 WSL2의 Ubuntu를 사용할 수 있습니다.

- C++20을 지원하는 GCC 또는 Clang
- CMake 3.20 이상
- Ninja 또는 Make
- POSIX Threads
- PostgreSQL 16 이상과 `libpq`
- 로컬 DB 자동 구성을 위한 Docker Engine과 Docker Compose plugin

Qt 데스크톱 클라이언트만 빌드할 때는 Linux, macOS, Windows에서 Qt 6.5
이상의 Widgets, Network, Test 구성 요소가 추가로 필요합니다.

Ubuntu에서는 다음 패키지로 시작할 수 있습니다.

```bash
sudo apt-get update
sudo apt-get install --yes \
  build-essential cmake libpq-dev ninja-build postgresql-client
```

## 빠른 시작

저장소에는 반복적인 개발 구성을 위한 `CMakePresets.json`도 포함되어
있습니다. Linux 전체 개발 빌드는 `linux-dev`, macOS와 Windows의 플랫폼
독립 빌드는 `core-dev`, 마이크로벤치마크는 `benchmark` preset을 사용합니다.
Qt 데스크톱 클라이언트는 `qt-client-dev` preset을 사용합니다.

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

Docker Compose를 사용하면 PostgreSQL 16 실행과 migration 적용을 한 번에
처리할 수 있습니다.

```bash
docker compose up -d
docker compose ps -a
```

`postgres`가 `healthy`, `migrate`가 종료 코드 `0`인 것을 확인합니다. migration
로그가 필요하면 `docker compose logs migrate`를 실행합니다. 기본 로컬 설정은
사용자 `rss`, 비밀번호 `local-password`, database `rss`, host port `5432`를
사용합니다. 포트가 이미 사용 중이면 다음과 같이 바꿀 수 있습니다.

```bash
RSS_POSTGRES_PORT=55432 docker compose up -d
```

이 경우 아래 `RSS_DATABASE_URL`의 port도 `55432`로 맞춥니다. 사용자, 비밀번호,
database는 각각 `RSS_POSTGRES_USER`, `RSS_POSTGRES_PASSWORD`,
`RSS_POSTGRES_DB`로 변경할 수 있습니다. 기본값은 로컬 개발 전용이며 운영
자격증명은 환경이나 secret manager에서 전달해야 합니다.

DB와 migration이 준비되면 서버를 실행합니다.

```bash
export RSS_DATABASE_URL='postgresql://rss:local-password@127.0.0.1:5432/rss'
./build/rss_server 0.0.0.0 7777 4
```

`RSS_DATABASE_URL`은 필수입니다. DB 작업 스레드 수는 `RSS_DB_WORKERS`(기본
`2`), 대기 queue 용량은 `RSS_DB_QUEUE_CAPACITY`(기본 `1024`)로 조정합니다.
값은 모두 서버 시작 전에 검증되며 DB 연결에 실패하면 listener를 열지
않습니다.

서버는 운영 로그를 한 줄에 JSON 객체 하나인 NDJSON 형식으로 출력합니다.
시작·주기 통계·정상 종료는 표준 출력, 시작 실패는 표준 오류에 기록합니다.
과부하 통계 출력 주기는 `RSS_OBSERVABILITY_INTERVAL_SECONDS`로 설정하며
기본값은 `30`초입니다. `0`이면 주기 출력만 끄고 시작·최종 통계·종료
이벤트는 계속 기록합니다.

```bash
RSS_OBSERVABILITY_INTERVAL_SECONDS=10 \
  ./build/rss_server 0.0.0.0 7777 4
```

각 로그에는 Unix epoch 밀리초인 `timestamp_unix_ms`, `level`, `event`가
포함됩니다. `overload_snapshot` 이벤트는 `periodic` 또는 `final` phase와
queue, 세션, 거절·종료·예외 누적값을 제공합니다. 사용자 이름, 방 이름,
채팅 payload와 database URL은 운영 로그에 기록하지 않습니다.

DB를 정지하되 데이터를 보존하려면 다음 명령을 사용합니다.

```bash
docker compose down
```

다시 `docker compose up -d`를 실행하면 named volume의 기존 사용자 UUID를
그대로 사용하고 migration을 재적용합니다. 로컬 DB 데이터까지 완전히
삭제하려는 경우에만 다음 명령을 사용합니다.

```bash
docker compose down -v
```

`down -v`로 삭제한 named volume의 데이터는 복구되지 않습니다. Docker를
사용하지 않는 환경에서는 PostgreSQL 16 database를 직접 준비한 뒤 기존처럼
`psql`로 migration을 적용할 수 있습니다.

```bash
psql "$RSS_DATABASE_URL" -v ON_ERROR_STOP=1 \
  -f libs/server-persistence-postgres/migrations/001_users.sql
```

인자는 순서대로 다음 의미입니다.

```text
rss_server <접속을 받을 주소> <포트> <worker 스레드 수>
```

인자를 생략하면 주소는 `0.0.0.0`, 포트는 `7777`을 사용합니다. worker
수는 명시하지 않으면 CPU가 제공하는 동시 실행 수를 기준으로 정합니다.

### 서버 종료

포그라운드에서 실행 중인 서버는 `Ctrl+C`로 종료할 수 있습니다. 다른
프로세스에서는 서버 PID에 `SIGTERM`을 보냅니다.

```bash
kill -TERM <server-pid>
```

두 신호는 새 연결과 입력을 중단하고 이미 받은 요청과 미전송 응답을 비우는
정상 종료를 요청합니다. 기본 5초인 `graceful_shutdown_timeout` 안에 drain이
끝나지 않으면 서버는 남은 작업을 중단하고 네트워크 자원을 정리합니다.

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

로그인 이름은 앞뒤 ASCII 공백을 제거하고 ASCII 대소문자를 구분하지 않는
조회 키로 사용합니다. 같은 이름으로 재접속하면 PostgreSQL에 저장된 같은
UUID를 받습니다. 현재 이름 로그인은 인증이 아니므로 이름을 아는 다른
클라이언트도 같은 사용자로 식별될 수 있습니다. 방, 참가 상태와 채팅
메시지는 아직 메모리에만 있으며 서버 재시작 시 복구되지 않습니다.

## Qt 데스크톱 클라이언트

Qt 클라이언트는 Linux, macOS, Windows에서 같은 소스로 빌드됩니다. 서버
주소와 포트를 입력해 연결한 뒤 로그인, 방 생성·참가·나가기, 채팅 송수신을
GUI에서 수행할 수 있습니다. 기본 주소는 `127.0.0.1`, 기본 포트는
`7777`입니다.

로컬 요청 인코딩 오류처럼 복구 가능한 전송 오류는 현재 연결을 유지합니다.
소켓과 수신 프로토콜의 복구 불가능한 오류는 실제 연결을 종료하고 화면 상태를
초기화하며, 전송에 실패한 채팅 입력은 사용자가 다시 시도할 수 있게 보존합니다.

### macOS

```bash
brew install qt
cmake --preset qt-client-dev -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build --preset qt-client-dev --parallel
ctest --preset qt-client-dev
./build/qt-client-dev/rss_qt_client.app/Contents/MacOS/rss_qt_client
```

### Linux

Ubuntu에서는 Qt 개발 패키지를 설치한 뒤 빌드합니다.

```bash
sudo apt-get install --yes qt6-base-dev qt6-base-dev-tools
cmake --preset qt-client-dev
cmake --build --preset qt-client-dev --parallel
QT_QPA_PLATFORM=offscreen ctest --preset qt-client-dev
./build/qt-client-dev/rss_qt_client
```

### Windows

Qt 온라인 설치 프로그램에서 MSVC 2022 64-bit 데스크톱 패키지를 설치한
예시입니다. 설치한 Qt 버전과 위치에 맞게 경로를 바꿉니다.

```powershell
cmake --preset qt-client-dev -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build --preset qt-client-dev --parallel
ctest --preset qt-client-dev
./build/qt-client-dev/rss_qt_client.exe
```

현재 Qt 클라이언트는 명시적인 수동 연결과 재연결만 지원합니다. 자동
재접속, 로그인·방 상태 자동 복구, 위치 전송, `PING`, TLS는 포함하지
않습니다.

## 부하 측정 도구

두 도구는 측정 대상이 다릅니다. 둘 다 Linux 전용입니다.

- `rss_load_test_client`는 이미 실행 중인 원격 서버에 연결해 `PING`/`PONG`
  왕복 지연 시간을 빠르게 확인합니다.
- `rss_load_scenario_runner`는 loopback 임시 포트에서 실제 서버를 직접
  실행하고, 재현 가능한 `broadcast`, `multi-room`, `slow-client` 시나리오와
  서버 내부 과부하 통계를 함께 기록합니다.

원격 서버의 연결과 요청/응답 상태만 확인하려면 다음 `PING` 부하 테스트를
사용합니다. 클라이언트 100개가 각각 `PING`을 100번 보내고 `PONG` 응답
시간을 측정합니다.

```bash
./build/rss_load_test_client 127.0.0.1 7777 100 100
```

출력에는 전송 수, 실패한 클라이언트 수, 초당 처리량과
`p50`/`p95`/`p99` 응답 시간이 포함됩니다. 자세한 측정 방법은
[벤치마크 가이드](docs/benchmark.md)를 참고하세요.

실제 서버의 방 broadcast, 다중 방, 느린 클라이언트 격리 측정은 Linux에서
`rss_load_scenario_runner`를 사용합니다. 이 도구는 warm-up 1회를 버린 뒤
같은 설정으로 반복 측정합니다. 서로 다른 commit의 결과는 CPU, 운영체제,
compiler, build type, worker 수와 실행 인자를 포함해 같은 환경에서만
비교하세요.

```bash
cmake --preset linux-dev
cmake --build --preset linux-dev --target rss_load_scenario_runner --parallel
./build/linux-dev/rss_load_scenario_runner --scenario broadcast --clients 2 --messages 2 --payload-bytes 128 --repeat 1 --workers 1
```

각 `run` 줄은 effective 방 수와 `messages_per_sender`, `payload_bytes`,
`slow_clients`, `repeats`를 고정 순서로 기록합니다. `expected`는 설정상 최대치가
아니라 방별 실제 성공 전송 수에서 계산합니다. client setup 실패도 결과 줄과
`failed_clients`를 남기며 종료 코드 `1`을 반환합니다.

## 서버가 요청을 처리하는 순서

1. I/O 스레드가 `epoll`로 소켓 이벤트를 기다립니다.
2. 소켓에서 읽은 바이트를 `PacketCodec`이 완전한 패킷으로 나눕니다.
3. 완성된 패킷을 작업 큐에 넣습니다.
4. worker 스레드가 로그인, 방, 채팅 같은 명령을 처리합니다.
5. worker가 응답을 출력 큐에 넣고 `eventfd`로 I/O 스레드를 깨웁니다.
6. I/O 스레드가 응답을 각 클라이언트 소켓으로 전송합니다.

소켓의 읽기, 쓰기, 연결 종료와 `epoll_ctl` 호출은 I/O 스레드만
담당합니다. worker 스레드는 소켓을 직접 조작하지 않습니다.

## 과부하 보호

입력·출력 queue, 세션별 미전송 byte, 동시 세션 수에는 `ServerConfig`로
조정하는 상한이 있습니다. 입력 이벤트 하나가 만드는 출력량도 제한합니다.
입력 처리량이 회복될 때까지 읽기를 일시정지하고, 이때 상대가 송신을
종료하면 수신 버퍼를 비운 뒤 연결 종료 이벤트를 전달합니다. 지속적으로
응답을 소비하지 않는 세션은 다른 세션과 분리합니다. 세부 정책과 통계의
의미는 [서버 구조](docs/architecture.md)를 참고하세요.

## 디렉터리 구조

```text
apps/server/            Linux epoll 서버 실행 파일
apps/console-client/    대화형 POSIX 콘솔 클라이언트
apps/load-test-client/  PING 부하 테스트 클라이언트
apps/load-scenario-runner/ Linux 실제 서버 부하 시나리오 실행기
apps/qt-client/         Qt 6 Widgets 데스크톱 클라이언트
libs/protocol/          패킷 종류와 인코딩 규칙
libs/server-core/       플랫폼 독립 도메인, 서비스, session, worker
libs/server-persistence-postgres/ PostgreSQL 사용자 저장소와 migration
libs/server-net-linux/  Linux epoll, eventfd, TCP 서버 구현
libs/load-test-support/ 부하 테스트 통계 지원 코드
tests/                  라이브러리별 GoogleTest 자동 테스트
benchmarks/             Google Benchmark 마이크로벤치마크
docs/                   구조, 프로토콜, 설계, 벤치마크 설명
```

## 현재 제약 사항

- epoll 서버와 POSIX 콘솔·부하 측정 도구는 Linux에서만 빌드됩니다.
- Qt 데스크톱 클라이언트는 Linux, macOS, Windows를 지원합니다.
- 방과 사용자 상태는 하나의 `RoomService` mutex로 보호합니다.
- `rss_load_test_client`는 `PING`/`PONG`만 측정하며, 실제 서버 시나리오는
  `rss_load_scenario_runner`가 측정합니다.
- TLS, 인증 토큰, 데이터 영속 저장은 구현되어 있지 않습니다.

## 더 자세한 문서

- [서버 구조](docs/architecture.md)
- [패킷 프로토콜](docs/protocol.md)
- [벤치마크 실행과 해석](docs/benchmark.md)
- [프로젝트 상태](docs/project-status.md)
- [알려진 문제](docs/known-issues.md)
- [로드맵](docs/roadmap.md)
- [개발 계획](docs/development-plan.md)
- [개발 및 코드 스타일](CONTRIBUTING.md)
