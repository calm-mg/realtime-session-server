# 운영 종료 신호와 정상 종료 연결 설계

## 목적

Linux 서버 실행 파일이 `SIGINT`와 `SIGTERM`을 받으면 즉시 종료하거나 예외로
빠지지 않고 `TcpServer::stop()`을 통해 기존 정상 종료 절차를 시작하도록
합니다. 운영자가 터미널의 `Ctrl+C` 또는 서비스 관리자의 종료 요청을 사용해
새 연결을 중단하고, 제한 시간 안에서 이미 수락한 입력과 생성된 출력을
drain한 뒤 프로세스를 끝낼 수 있어야 합니다.

이 문서는 운영체제 신호를 서버 종료 요청으로 변환하는 애플리케이션 계층의
책임과 검증 범위를 정의합니다. `TcpServer`의 현재 종료 단계와 제한 시간
동작은 [서버 구조](../architecture.md), 작업 순서와 열린 결함은 각각
[프로젝트 상태](../project-status.md)와 [알려진 문제](../known-issues.md)를
기준으로 합니다.

## 현재 문제

`TcpServer::stop()`은 atomic 종료 플래그를 설정하고 `eventfd`를 통해 I/O
스레드를 깨웁니다. I/O 스레드는 이후 새 연결과 입력을 막고, 이미 받은 입력,
worker 출력과 세션의 미전송 응답을 `graceful_shutdown_timeout` 동안
drain합니다. 하지만 `apps/server/src/main.cpp`는 프로세스 종료 신호를 이
메서드에 연결하지 않습니다. 기본 signal disposition에 따라 `SIGINT`나
`SIGTERM`을 받으면 프로세스가 즉시 끝나므로 기존 정상 종료 절차가 실행되지
않습니다.

일반적인 비동기 signal handler에서 `TcpServer::stop()`을 직접 호출할 수도
없습니다. `stop()`은 C++ atomic과 `EventFdCompletionNotifier`를 사용하며 이
전체 호출을 async-signal-safe하다고 보장할 수 없습니다. 또한 현재
`EpollEventLoop::wait()`는 `epoll_wait()`의 `EINTR`을 일반 오류와 구분하지
않으므로, I/O 스레드가 신호를 직접 받으면 정상 종료 대신 예외 경로로 나갈
수 있습니다.

## 결정

### 신호는 전용 스레드에서 동기적으로 기다림

서버 실행 파일은 비동기 signal handler를 설치하지 않습니다. 대신
애플리케이션 계층의 종료 신호 구성 요소가 다음 순서로 동작합니다.

1. `SIGINT`와 `SIGTERM`을 포함하는 `sigset_t`를 만듭니다.
2. `blockShutdownSignals()`가 `TcpServer` 생성과 `run()`보다 먼저 호출
   스레드에서 두 신호를 `pthread_sigmask(SIG_BLOCK, ...)`로 차단합니다.
3. 이후 생성되는 monitor와 worker 스레드는 차단된 signal mask를
   상속합니다.
4. `ShutdownSignalMonitor`의 전용 스레드만 `sigwait()`로 두 신호를
   동기적으로 기다립니다.
5. 신호를 받으면 일반 스레드 문맥에서 `TcpServer::stop()`을 호출합니다.

이 방식에는 비동기 signal handler가 없으므로 handler 안에서 호출 가능한
함수를 제한하거나 전역 pipe·file descriptor를 관리할 필요가 없습니다.
`epoll_wait()`도 종료 신호로 중단되지 않으며, `stop()`이 기록하는
`eventfd`가 기존 방식대로 I/O 스레드를 즉시 깨웁니다.

`blockShutdownSignals()`와 `ShutdownSignalMonitor`는 Linux 전용 서버 실행
파일의 프로세스 수명 구성 요소입니다. monitor는 `TcpServer&`와 신호 집합을
받아 신호 수신 시 `stop()`만 호출합니다. `server-core`와 재사용 가능한
`server-net-linux` 자체는 프로세스의 signal disposition을 소유하지
않습니다. 따라서 부하 시나리오의 embedded server나 라이브러리 사용자는
기존처럼 명시적으로 `TcpServer::stop()`을 호출합니다.

### 종료 신호의 의미

`SIGINT`와 `SIGTERM`은 동일하게 정상 종료 요청 하나를 의미합니다.
monitor는 drain이 진행되는 동안에도 두 신호를 계속 소비하고
`TcpServer::stop()`을 다시 호출할 수 있습니다. `stop()`은 멱등적이므로 종료
단계나 deadline을 다시 시작하지 않습니다.

두 번째 신호를 즉시 강제 종료 요청으로 승격하지 않습니다. 강제 종료 시점은
기존 `ServerConfig::graceful_shutdown_timeout`과 `TcpServer` 상태 머신이
유일하게 결정합니다. 별도의 강제 종료 public API나 새로운 실행 인자는
추가하지 않습니다.

### 수명과 오류 처리

`blockShutdownSignals()`는 `TcpServer`보다 먼저 호출하고,
`ShutdownSignalMonitor`는 `TcpServer`가 생성된 뒤 `run()`을 호출하기 전에
생성합니다. 지역 변수 파괴 순서에 따라 monitor가 먼저 대기 스레드를 join한
뒤 `TcpServer`가 파괴됩니다. monitor가 이미 파괴된 서버를 참조할 수
없습니다.

monitor 생성 중 signal mask 설정이나 스레드 생성이 실패하면 서버를
시작하지 않고 기존 `main()` 예외 처리로 실패 메시지와 `EXIT_FAILURE`를
반환합니다. `sigwait()` 자체가 실패하면 monitor는 `TcpServer::stop()`을
요청해 서버를 정리한 뒤 오류 상태를 보관합니다. `run()`이 반환한 다음
애플리케이션은 이 상태를 확인해 정상적인 운영 신호 종료와 내부 대기 오류를
구분합니다.

monitor를 파괴할 때는 종료 플래그를 설정하고 대상 monitor 스레드에 차단된
종료 신호를 보내 `sigwait()`를 깨운 뒤 join합니다. 이 내부 wake-up은 서버
종료를 다시 요청하지 않습니다. monitor는 서버 프로세스 전체 수명에만
사용되며, 종료 신호의 기본 disposition을 다시 활성화해 이미 pending인
신호가 정리 도중 프로세스를 종료시키지 않도록 signal mask를 복원하지
않습니다. monitor 파괴 직후 `main()`은 반환하고 프로세스가 끝납니다.

## 종료 흐름

```text
SIGINT 또는 SIGTERM
        |
        v
ShutdownSignalMonitor::waitLoop()
        |
        v
TcpServer::stop()
  - stop_requested = true
  - eventfd notify
        |
        v
I/O 스레드가 beginShutdown() 실행
        |
        +--> DrainingInput --> DrainingOutput --> Complete
        |
        `--> 제한 시간 초과 --> Forced --> Complete
```

프로세스 exit code는 정상적인 `SIGINT`·`SIGTERM` 종료와 일반적인 정상 반환
모두 `EXIT_SUCCESS`입니다. signal mask 설정, monitor 대기 또는 서버 실행
중 내부 오류가 발생한 경우에는 `EXIT_FAILURE`입니다.

## 구성 요소와 변경 범위

### 서버 애플리케이션

- `apps/server`에 `blockShutdownSignals()`와 `ShutdownSignalMonitor` 선언과
  구현 추가
- `main.cpp`에서 `TcpServer` 생성 후 monitor를 연결하고 `run()` 실행
- monitor 구현을 `rss_server` executable에만 포함하고 재사용 가능한 서버
  라이브러리의 public include나 link interface는 변경하지 않음

### 테스트

- 실제 `rss_server` 자식 프로세스를 ephemeral loopback port로 실행
- listener가 준비된 뒤 `SIGINT` 또는 `SIGTERM` 전달
- `graceful_shutdown_timeout`을 포함하는 상한 안에 자식이 종료되는지 확인
- signal에 의해 직접 사망한 상태가 아니라 `EXIT_SUCCESS`로 반환하는지 확인
- 실패 시 자식 프로세스를 반드시 회수하고, 제한 시간을 넘긴 자식만
  `SIGKILL`로 정리해 테스트 실행을 누수 없이 종료

기존 `TcpServerBackpressureTest`가 입력·출력 drain, timeout 강제 종료와
종료 중 세션 동작을 계속 검증합니다. 새 프로세스 테스트는 그 로직을
중복하지 않고 운영체제 신호가 기존 `stop()` 진입점에 연결되는 경계만
검증합니다.

### 문서

- README의 서버 실행 절차에 `Ctrl+C`, `kill -TERM`과 drain timeout 설명 추가
- 서버 구조 문서에 운영 신호에서 상태 머신까지의 진입 경로 추가
- 알려진 문제에서 해결된 종료 신호 항목 제거
- 프로젝트 상태의 최근 완료 이력과 다음 운영 안정성 우선순위 갱신

## 범위 밖

- `SIGHUP` 기반 설정 재적재
- 두 번째 종료 신호에 의한 즉시 강제 종료
- Windows service control 또는 macOS 전용 종료 처리
- systemd unit, container manifest와 배포 스크립트 추가
- `TcpServer` 종료 단계나 `graceful_shutdown_timeout` 의미 변경
- 구조화된 로그 또는 종료 통계 형식 추가

## 테스트 시나리오

### 프로세스 통합 동작

- 실행 중인 `rss_server`에 `SIGINT`를 보내면 제한 시간 안에 exit code 0으로
  종료
- 실행 중인 `rss_server`에 `SIGTERM`을 보내면 제한 시간 안에 exit code
  0으로 종료
- listener 준비를 확인한 뒤 신호를 보내 시작 전 실패를 정상 종료로
  오인하지 않음
- 정상 종료 과정에서 monitor 대기 스레드가 join되어 자식 프로세스가
  남지 않는지 확인
- timeout과 비정상 종료 시 자식의 wait status를 assertion에 포함하고, 자식이
  상속한 표준 오류를 테스트 출력에서 확인 가능하게 유지

signal mask와 process signal은 프로세스 전역 상태이므로 동일한 GoogleTest
프로세스 안에서 직접 발생시키는 단위 테스트는 두지 않습니다. 별도
`rss_server` 자식 프로세스가 신호 차단, `sigwait()`, `stop()`, monitor join과
정상 exit를 하나의 운영 경로로 검증합니다. `TcpServer::stop()`의 멱등성과
drain deadline은 기존 `TcpServerBackpressureTest`가 검증합니다.

### 회귀 검증

- Linux `linux-dev` configure, build와 전체 test
- `format-check`
- 가능한 경우 `tidy-check`
- 플랫폼 독립 타깃과 Qt 클라이언트 소스에는 변경이 없음을 확인

## 완료 조건

- `SIGINT`와 `SIGTERM`이 비동기 handler 없이 전용 `sigwait()` 스레드에서
  안전하게 소비됩니다.
- 운영 종료 신호가 `TcpServer::stop()`을 호출하고 기존 drain 상태 머신을
  시작합니다.
- I/O 스레드의 `epoll_wait()`가 운영 종료 신호로 `EINTR` 오류 경로에
  들어가지 않습니다.
- 두 신호에 대한 실제 프로세스 통합 테스트가 제한 시간 내 정상 종료를
  검증합니다.
- embedded server와 `TcpServer` public API의 동작이 바뀌지 않습니다.
- README, 서버 구조, 알려진 문제와 프로젝트 상태가 구현 결과와 일치합니다.
