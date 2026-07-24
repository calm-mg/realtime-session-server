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
  ninja-build
```

## Debug 빌드

```bash
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dev --parallel
```

CMake는 clangd가 읽을 수 있는 `build/dev/compile_commands.json`도
생성합니다. 처음 구성할 때 GoogleTest를 내려받으므로 인터넷 연결이
필요합니다. 내려받은 파일은 `build/dev/_deps` 아래에만 저장됩니다.

## 테스트

테스트는 GoogleTest로 작성되어 있으며 CTest에서 각 테스트 사례를
개별적으로 찾습니다.

```bash
ctest --test-dir build/dev --output-on-failure
```

등록된 테스트 이름을 먼저 보고 싶다면 다음 명령을 실행합니다.

```bash
ctest --test-dir build/dev -N
```

특정 테스트만 실행하려면 테스트 이름을 정규식으로 지정합니다.

```bash
ctest --test-dir build/dev -R 'PacketCodecTest\.' --output-on-failure
```

GoogleTest 필터와 상세 출력을 직접 사용하려면 테스트 실행 파일을
실행합니다.

```bash
./build/dev/rss_core_tests \
  --gtest_filter='PacketCodecTest.*' \
  --gtest_color=yes
```

Linux 네트워크 테스트는 별도 실행 파일에 들어 있습니다.

```bash
./build/dev/rss_net_tests --gtest_color=yes
```

## 마이크로벤치마크

Google Benchmark 기반 마이크로벤치마크는 기본 빌드에서 꺼져 있습니다.
Release 빌드에서 다음 옵션으로 켭니다.

```bash
cmake -S . \
  -B build/benchmark \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DRSS_BUILD_BENCHMARKS=ON

cmake --build build/benchmark --target rss_microbenchmarks --parallel
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
cmake --build build/dev --target format
```

파일을 수정하지 않고 포맷 위반만 검사합니다.

```bash
cmake --build build/dev --target format-check
```

Google clang-tidy 규칙을 검사합니다.

```bash
cmake --build build/dev --target tidy-check
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
cmake -S . \
  -B build/macos \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRSS_BUILD_NETWORK_TARGETS=OFF

cmake --build build/macos --parallel
ctest --test-dir build/macos --output-on-failure
```

이 설정으로는 `rss_server`, `rss_console_client`,
`rss_load_test_client`를 빌드하거나 Linux 네트워크 코드를 디버깅할 수
없습니다.

## 변경 전 확인 사항

코드를 공유하기 전에 다음 명령이 모두 통과하는지 확인합니다.

```bash
cmake --build build/dev --target format-check
cmake --build build/dev --target tidy-check
cmake --build build/dev --parallel
ctest --test-dir build/dev --output-on-failure
cmake -S . -B build/benchmark -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DRSS_BUILD_BENCHMARKS=ON
cmake --build build/benchmark --target rss_microbenchmarks --parallel
./build/benchmark/rss_microbenchmarks --benchmark_dry_run
```

프로토콜 동작을 변경했다면 `docs/protocol.md`도 함께 수정합니다. 실행
인자나 빌드 방법을 변경했다면 `README.md`의 빠른 시작도 확인합니다.
