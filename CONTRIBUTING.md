# 개발 가이드

이 문서는 코드를 수정하고 검사할 때 필요한 명령을 설명합니다.

## 개발 환경

서버는 Linux의 `epoll`과 `eventfd`를 사용합니다. 전체 기능을 개발하려면
Linux 또는 Windows WSL2의 Ubuntu 환경을 사용하세요.

필요한 도구는 다음과 같습니다.

- C++20을 지원하는 GCC 또는 Clang
- CMake 3.20 이상
- Ninja
- Clang 18의 `clang-format`, `clang-tidy`, `clangd`
- GDB
- PostgreSQL 16 이상, `libpq` 개발 header와 `psql`

Ubuntu 24.04에서는 다음 명령으로 설치할 수 있습니다.

```bash
sudo apt-get update
sudo apt-get install --yes \
  build-essential \
  clang-18 \
  clangd-18 \
  clang-format-18 \
  clang-tidy-18 \
  cmake \
  gdb \
  libpq-dev \
  postgresql-client \
  ninja-build
```

## Debug 빌드

```bash
cmake --preset linux-dev
cmake --build --preset linux-dev --parallel
```

CMake는 clangd가 읽을 수 있는 `build/linux-dev/compile_commands.json`도
생성합니다. 처음 구성할 때 GoogleTest를 내려받으므로 인터넷 연결이
필요합니다. 내려받은 파일은 해당 build 디렉터리의 `_deps` 아래에만
저장됩니다.

## 테스트

테스트는 GoogleTest로 작성되어 있으며 CTest에서 각 테스트 사례를
개별적으로 찾습니다.

```bash
ctest --preset linux-dev
```

Linux 전체 테스트의 PostgreSQL 통합 항목을 실행하려면 테스트 DB에 migration을
적용하고 URL을 전달합니다. 저장소에 실제 비밀번호를 기록하지 마세요.

```bash
export RSS_TEST_DATABASE_URL='postgresql://rss:local-password@127.0.0.1:5432/rss_test'
psql "$RSS_TEST_DATABASE_URL" -v ON_ERROR_STOP=1 \
  -f libs/server-persistence-postgres/migrations/001_users.sql
ctest --preset linux-dev
```

환경 변수가 없으면 실제 DB가 필요한 repository와 server process 테스트만
명시적으로 건너뜁니다. PostgreSQL adapter compile, 잘못된 연결 처리와 코어
테스트는 계속 실행됩니다.

등록된 테스트 이름을 먼저 보고 싶다면 다음 명령을 실행합니다.

```bash
ctest --test-dir build/linux-dev -N
```

특정 테스트만 실행하려면 테스트 이름을 정규식으로 지정합니다.

```bash
ctest --test-dir build/linux-dev -R 'PacketCodecTest\.' --output-on-failure
```

GoogleTest 필터와 상세 출력을 직접 사용하려면 테스트 실행 파일을
실행합니다.

```bash
./build/linux-dev/tests/protocol/rss_protocol_tests \
  --gtest_filter='PacketCodecTest.*' \
  --gtest_color=yes
```

Linux 네트워크 테스트는 별도 실행 파일에 들어 있습니다.

```bash
./build/linux-dev/tests/server-net-linux/rss_server_net_tests \
  --gtest_color=yes
```

## 실제 서버 부하 시나리오

`rss_load_scenario_runner`는 실제 `TcpServer`와 POSIX TCP 클라이언트를 함께
구동하는 Linux 전용 실행 파일입니다. 원격 서버의 `PING` 왕복 확인이 아니라
로컬 broadcast, 다중 방, 느린 클라이언트 격리 회귀를 측정할 때 사용합니다.

Linux 개발 preset에서 target을 빌드하고 작은 broadcast smoke를 실행합니다.

```bash
cmake --preset linux-dev
cmake --build --preset linux-dev --target rss_load_scenario_runner --parallel
./build/linux-dev/rss_load_scenario_runner \
  --scenario broadcast --clients 2 --messages 2 \
  --payload-bytes 128 --repeat 1 --workers 1
```

출력의 `missing`, `duplicates`, `unexpected`, `failed_clients`가 모두 `0`인지
확인합니다. 성능 수치는 같은 CPU, Linux kernel, compiler, build type, worker
수와 실행 인자에서 얻은 결과끼리만 비교합니다. 세 시나리오의 상세 인자와
필드 의미는 `docs/benchmark.md`를 참고하세요.

## 마이크로벤치마크

Google Benchmark 기반 마이크로벤치마크는 기본 빌드에서 꺼져 있습니다.
Release 빌드에서 다음 옵션으로 켭니다.

```bash
cmake --preset benchmark
cmake --build --preset benchmark --target rss_microbenchmarks --parallel
./build/benchmark/rss_microbenchmarks
```

특정 항목만 실행하려면 이름 정규식을 사용합니다.

```bash
./build/benchmark/rss_microbenchmarks \
  --benchmark_filter='BM_Packet(Encode|Decode)'
```

모든 항목이 실행 가능한지만 빠르게 확인할 때는 실제 반복 측정 대신
dry-run을 사용합니다.

```bash
./build/benchmark/rss_microbenchmarks --benchmark_dry_run
```

## Google C++ 스타일

이 프로젝트는 Google C++ Style Guide를 기준으로 사용합니다.

- `.clang-format`: 들여쓰기, 줄바꿈 같은 자동 포맷 규칙
- `.clang-tidy`: 자동으로 검사할 수 있는 `google-*` 규칙
- CI: push와 pull request에서 포맷, 정적 분석, 빌드, 테스트 검사

전체 C++ 파일을 자동으로 수정합니다.

```bash
cmake --build --preset linux-dev --target format
```

파일을 수정하지 않고 포맷 위반만 검사합니다.

```bash
cmake --build --preset linux-dev --target format-check
```

Google clang-tidy 규칙을 검사합니다.

```bash
cmake --build --preset linux-dev --target tidy-check
```

`format`은 파일을 변경합니다. `format-check`와 `tidy-check`는 검사만
실행하고 문제가 있으면 실패 코드로 종료합니다.

자동 도구로 API 설계, 소유권, 이름의 의도까지 모두 판단할 수는
없습니다. 도구가 검사하지 못하는 항목은 코드 리뷰에서 확인합니다.

## VS Code

저장소를 열면 `.vscode/extensions.json`에 등록된 확장을 설치하도록
VS Code가 안내합니다.

- clangd: 코드 탐색, 자동 완성, 진단, 포맷
- CMake Tools: CMake 구성과 빌드
- C/C++: GDB 디버깅

Windows에서는 프로젝트를 일반 Windows 폴더로 열지 말고
`WSL: Reopen Folder in WSL`을 사용하세요.

## macOS에서 코어만 빌드

macOS에는 `epoll`과 `eventfd`가 없으므로 네트워크 타깃을 끄고
플랫폼 독립적인 코드와 테스트만 빌드할 수 있습니다.

```bash
cmake --preset core-dev
cmake --build --preset core-dev --parallel
ctest --preset core-dev
```

이 설정으로는 `rss_server`, `rss_console_client`,
`rss_load_test_client`, `rss_load_scenario_runner`를 빌드하거나 Linux
네트워크 코드를 디버깅할 수 없습니다.

## Qt 데스크톱 클라이언트 개발

Qt 클라이언트는 Qt 6.5 이상의 Widgets, Network, Test 구성 요소를
사용합니다. 플랫폼별 준비 방법은 다음과 같습니다.

- Linux: `qt6-base-dev`, `qt6-base-dev-tools` 설치
- macOS: `brew install qt`
- Windows: Qt 온라인 설치 프로그램의 MSVC 2022 64-bit 데스크톱 패키지와
  Ninja 설치

macOS에서는 Homebrew Qt 경로를 CMake에 전달합니다.

```bash
cmake --preset qt-client-dev -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build --preset qt-client-dev --parallel
ctest --preset qt-client-dev
```

Linux와 Windows에서 Qt가 표준 검색 경로나 현재 셸 환경에 등록되어 있다면
추가 경로 없이 같은 preset을 사용할 수 있습니다. GUI가 없는 Linux 환경은
다음처럼 offscreen 플랫폼으로 Widget 테스트를 실행합니다.

```bash
QT_QPA_PLATFORM=offscreen ctest --preset qt-client-dev
```

화면 배치는 `apps/qt-client/ui/src/MainWindow.ui`, 상태와 사용자 동작은
`apps/qt-client/ui/src/MainWindow.cpp`, TCP 통신은 `apps/qt-client/network`에서
관리합니다. 생성 파일인
`ui_MainWindow.h`는 빌드 디렉터리에만 두며 커밋하지 않습니다.

## 변경 전 확인 사항

코드를 공유하기 전에 다음 명령이 모두 통과하는지 확인합니다.

```bash
cmake --preset linux-dev
cmake --build --preset linux-dev --target format-check
cmake --build --preset linux-dev --target tidy-check
cmake --build --preset linux-dev --parallel
ctest --preset linux-dev
cmake --preset benchmark
cmake --build --preset benchmark --target rss_microbenchmarks --parallel
./build/benchmark/rss_microbenchmarks --benchmark_dry_run
```

프로토콜 동작을 변경했다면 `docs/protocol.md`도 함께 수정합니다. 실행
인자나 빌드 방법을 변경했다면 `README.md`의 빠른 시작도 확인합니다.
Qt 클라이언트를 변경했다면 `qt-client-dev` 전체 테스트도 함께 실행합니다.
