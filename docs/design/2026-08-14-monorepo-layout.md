# 모노레포 구조 재편 설계

## 개요

새 클라이언트 애플리케이션을 추가하기 전에 저장소를 역할이 분명한
애플리케이션, 라이브러리, 테스트, 벤치마크 단위로 재구성한다. 이번 작업은
파일의 물리적 소유 위치와 CMake 구성을 변경하지만 기존 프로토콜, 실행
동작, 공개 C++ 이름, 실행 파일 이름은 유지한다.

이번 변경에는 Qt 클라이언트를 포함하지 않는다. 모노레포 구조를 먼저
병합하고 검증한 뒤, 별도 변경으로 크로스플랫폼 GUI 클라이언트를 추가한다.

## 목표

- 각 애플리케이션과 재사용 라이브러리에 명확한 디렉터리 경계를 부여한다.
- 파일 구조와 CMake에서 의존성 방향을 쉽게 확인할 수 있게 한다.
- Linux 전용 네트워크 코드를 크로스플랫폼 코드와 분리한다.
- 기존 서버, 콘솔 클라이언트, 부하 테스트 클라이언트, 테스트, 벤치마크를
  그대로 유지한다.
- macOS와 Windows에서 플랫폼 독립 타깃을 빌드하고 테스트할 수 있게 한다.
- Linux에서 전체 서버 스택을 빌드하고 테스트할 수 있게 한다.
- 저장소 전용 개발 규칙을 루트 `AGENTS.md`에 기록한다.

## 제외 범위

- Qt 또는 GUI 코드 추가
- 패킷 프로토콜이나 payload 형식 변경
- 서버 동작, 제한값, 스레딩, 종료 처리 변경
- 네임스페이스, 공개 클래스, 기존 실행 파일 이름 변경
- 파일 소유권과 무관한 구현 리팩터링
- 새로운 제품 기능 추가

## 목표 디렉터리 구조

```text
realtime-session-server/
├── AGENTS.md
├── CMakeLists.txt
├── CMakePresets.json
├── apps/
│   ├── server/
│   │   ├── CMakeLists.txt
│   │   └── src/main.cpp
│   ├── console-client/
│   │   ├── CMakeLists.txt
│   │   └── src/main.cpp
│   └── load-test-client/
│       ├── CMakeLists.txt
│       └── src/main.cpp
├── libs/
│   ├── protocol/
│   │   ├── CMakeLists.txt
│   │   ├── include/rss/protocol/
│   │   └── src/
│   ├── server-core/
│   │   ├── CMakeLists.txt
│   │   ├── include/rss/{domain,net,service,util}/
│   │   └── src/{domain,net,service}/
│   ├── server-net-linux/
│   │   ├── CMakeLists.txt
│   │   ├── include/rss/net/
│   │   └── src/
│   └── load-test-support/
│       ├── CMakeLists.txt
│       └── include/rss/tools/
├── tests/
│   ├── protocol/
│   ├── server-core/
│   ├── load-test-support/
│   └── server-net-linux/
├── benchmarks/
├── cmake/
├── docs/
└── .github/workflows/
```

## 소유권과 파일 이동

### 애플리케이션

- `apps/server`는 Linux 서버 진입점을 소유하며 Linux 서버 네트워크
  라이브러리에만 연결한다.
- `apps/console-client`는 기존 POSIX 콘솔 클라이언트를 소유하며 프로토콜
  라이브러리와 Threads에 연결한다. 현재 소켓 구현이 POSIX API를 사용하므로
  Linux 타깃으로 유지한다.
- `apps/load-test-client`는 기존 POSIX 부하 테스트 실행 파일을 소유하며
  프로토콜 및 부하 테스트 지원 라이브러리에 연결한다. 이 타깃도 Linux에서만
  빌드한다.

### 라이브러리

- `libs/protocol`은 `Packet`, `PacketCodec`, `PacketTypes`와 해당 구현을
  소유한다. 서버나 플랫폼 네트워크 코드에 의존하지 않는 C++20
  크로스플랫폼 라이브러리다.
- `libs/server-core`는 도메인과 서비스 코드, 그리고 `Session`,
  `WorkerPool`, `CompletionNotifier`, 과부하 통계, backpressure 설정,
  `BoundedBlockingQueue` 같은 플랫폼 독립 서버 지원 코드를 소유한다.
- `libs/server-net-linux`는 `EpollEventLoop`,
  `EventFdCompletionNotifier`, `TcpServer`와 Linux 전용 detail 헤더를
  소유한다.
- `libs/load-test-support`는 부하 테스트 실행 파일, 테스트, 벤치마크가
  공유하는 헤더 전용 latency 통계 코드를 소유한다.

### 테스트와 벤치마크

- 패킷 codec 테스트는 `tests/protocol`로 이동한다.
- 도메인, 서비스, queue, session, worker, 설정, 과부하 통계 테스트는
  `tests/server-core`로 이동한다.
- latency 통계 테스트는 `tests/load-test-support`로 이동한다.
- epoll, eventfd, TCP 서버 테스트는 `tests/server-net-linux`로 이동한다.
- 마이크로벤치마크 소스는 `benchmark`에서 `benchmarks`로 이동한다.

## 의존성 규칙

의존성은 다음 방향으로만 흐른다.

```text
apps/server             -> libs/server-net-linux
apps/console-client     -> libs/protocol
apps/load-test-client   -> libs/protocol + libs/load-test-support
libs/server-net-linux   -> libs/server-core + libs/protocol
libs/server-core        -> libs/protocol
benchmarks              -> 측정 대상 라이브러리
tests                   -> 테스트 대상 라이브러리
```

라이브러리는 애플리케이션에 의존할 수 없다. 플랫폼 독립 라이브러리는 Linux
전용 라이브러리에 의존할 수 없다. 프로토콜 라이브러리는 서버 도메인이나
서버 네트워크 코드에 의존할 수 없다.

## CMake 설계

루트 `CMakeLists.txt`는 superproject 진입점으로 유지한다. 프로젝트 공통
언어 설정과 빌드 옵션을 정의하고, 의존성과 코드 품질 helper를 불러온 뒤
현재 플랫폼에 해당하는 하위 디렉터리를 추가한다.

각 애플리케이션과 라이브러리는 자신의 소스, include 경로, compile feature,
경고 옵션, 직접 link dependency를 로컬 `CMakeLists.txt`에서 선언한다.
타깃 이름은 다음과 같다.

- `rss_protocol`
- `rss_server_core`
- `rss_server_net`
- `rss_load_test_support`
- `rss_server`
- `rss_console_client`
- `rss_load_test_client`

기존 빌드 옵션은 유지한다.

- `RSS_BUILD_TESTS`
- `RSS_BUILD_NETWORK_TARGETS`
- `RSS_BUILD_BENCHMARKS`

네트워크 타깃이 활성화되어 있고 `CMAKE_SYSTEM_NAME`이 `Linux`일 때만
Linux 전용 라이브러리, 애플리케이션, 테스트를 추가한다. 다른 플랫폼에서는
기존 안내 메시지를 출력한 뒤 플랫폼 독립 타깃 구성을 계속한다.

`CMakePresets.json`에는 플랫폼 독립 개발, Linux 개발, Release,
benchmark용 configure 및 build preset을 둔다. 사용자별 경로나 로컬 Qt
설치 경로는 preset에 저장하지 않는다.

## 동작 보존

이번 리팩터링은 파일 이동과 CMake 타깃 분리로만 구성한다. 소스의 실행
동작은 변경하지 않는다. 기존 `rss/...` include 표기를 유지하므로 이동된
코드에서 네임스페이스나 include API를 변경할 필요가 없다.

실행 파일 이름 `rss_server`, `rss_console_client`,
`rss_load_test_client`를 유지한다. 기존 명령행 인자와 기본값도 변경하지
않는다.

CTest는 기존처럼 개별 GoogleTest case를 발견한다. 테스트 실행 파일은
`rss_protocol_tests`, `rss_server_core_tests`, `rss_server_net_tests`로
분리하지만 개별 test suite와 test case 이름은 유지한다.

## 문서 작성과 에이전트 지침

README, 개발 가이드, 아키텍처 문서, 벤치마크 문서, VS Code 설정은 경로나
빌드 명령이 달라지는 부분만 수정한다.

공개 저장소의 `AGENTS.md`와 프로젝트 문서는 원칙적으로 한글로 작성한다.
코드 식별자, 파일 및 디렉터리 이름, 명령, API 이름, 업계에서 통용되는
기술 용어는 정확성을 위해 영어를 사용할 수 있다. 외부 기여자가 반드시
읽어야 하는 문서에는 필요할 경우 짧은 영문 요약을 추가할 수 있지만, 한글
본문을 기준 문서로 유지한다.

루트 `AGENTS.md`에는 다음 내용을 기록한다.

- 디렉터리 소유권과 허용된 의존성 방향
- 플랫폼별 타깃 지원 범위
- configure, build, test, format, tidy, benchmark 명령
- 프로토콜 변경 시 필요한 문서 및 테스트 작업
- 서버에서 이미 강제하는 스레드와 소켓 소유권 불변식
- 공개 저장소에 비밀정보, 인증정보, 개인적 배경, 무관한 업무 맥락을
  기록하지 않는 규칙
- `AGENTS.md`와 프로젝트 문서를 한글 중심으로 유지하는 규칙

`AGENTS.md`에는 코드베이스 유지보수에 필요하지 않은 사적인 설명이나
배경정보를 포함하지 않는다.

## CI와 검증

GitHub Actions matrix는 macOS와 Windows에서 플랫폼 독립 타깃을 계속
검증한다. Linux에서는 모든 타깃을 검증한다. 승인 기준은 다음과 같다.

1. macOS에서 플랫폼 독립 Debug 타깃을 구성하고 빌드한다.
2. macOS에서 모든 플랫폼 독립 테스트를 실행한다.
3. Linux CI에서 전체 프로젝트를 구성, 빌드, 테스트한다.
4. Windows CI에서 플랫폼 독립 타깃을 구성, 빌드, 테스트한다.
5. 누락되거나 이름이 바뀐 GoogleTest case 없이 기존 테스트를 모두 실행한다.
6. 마이크로벤치마크를 구성하고 빌드한 뒤 `--benchmark_dry_run`을 실행한다.
7. Linux CI에서 `format-check`와 `tidy-check`를 실행한다.
8. Git 작업 트리에 의도한 파일 이동과 빌드, 문서, 설정 변경만 있는지
   확인한다.

현재 장비에서 실행할 수 있는 macOS 검증은 변경을 공개하기 전에 반드시
통과해야 한다. Linux 전용 동작은 기존 테스트 소스를 그대로 유지하고 CI에서
추가로 검증한다.

## 마이그레이션 순서

1. 루트 지침과 CMake superproject 골격을 추가한다.
2. 프로토콜 파일을 이동하고 프로토콜 테스트를 복구한다.
3. 서버 코어 파일을 이동하고 플랫폼 독립 서버 코어 테스트를 복구한다.
4. Linux 네트워크 파일을 이동하고 Linux 전용 타깃과 테스트를 복구한다.
5. 세 애플리케이션 진입점과 부하 테스트 지원 코드를 이동한다.
6. 벤치마크를 이동하고 코드 품질 검사 대상 경로를 갱신한다.
7. preset을 추가하고 문서와 CI 경로를 갱신한다.
8. 실행 가능한 전체 검증을 수행하고 의도하지 않은 동작 변경이 없는지 최종
   diff를 검토한다.

각 중간 단계에서도 include 경로와 타깃 의존성을 명시적으로 유지한다. 최종
변경은 pull request로 검토한 뒤 `main`에 병합하며, `main`에 직접 커밋하지
않는다.

## 위험과 대응

- **파일 이동 중 소스나 테스트 누락:** 변경 전후 파일 목록과 CTest case
  수를 비교한다.
- **숨겨진 전이 의존성:** 각 타깃에 직접 의존성만 명시하고 깨끗한 빌드
  디렉터리에서 빌드한다.
- **플랫폼 회귀:** 세 운영체제 CI matrix를 유지하고 POSIX 및 Linux 전용
  애플리케이션을 명시적으로 격리한다.
- **불필요하게 복잡한 Git 이력:** 기계적인 이동과 내용 수정을 구분하고,
  관련 없는 formatting 변경을 만들지 않는다.
- **오래된 문서 경로:** 이동 후 저장소 전체에서 이전 디렉터리 경로를
  검색하고 영향받은 문서만 수정한다.

## 완료 조건

목표 구조와 의존성 규칙이 적용되고, 공개 문서가 새로운 경로와 일치하며,
실행 가능한 로컬 검증이 모두 통과하고, CI가 변경 전과 같거나 더 넓은 동작을
검사해야 한다. Pull request에는 Qt 코드나 의도적인 런타임 동작 변경이 없어야
한다.
