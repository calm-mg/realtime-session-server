# 개발 계획

현재 서버는 세션 관리, 방 기능, 바이너리 패킷 처리와 기본 부하 테스트를
제공합니다. 다음 작업은 테스트 기반을 정리한 뒤 과부하 상황을 제어하고,
마지막으로 실제 네트워크 시나리오를 반복 측정하는 순서로 진행합니다.

각 주차는 독립적으로 검증할 수 있는 결과물을 만들며, 완료되지 않은
항목은 다음 주차로 넘기지 않습니다.

## 1주차: 테스트와 측정 기반

> 상태: 완료 (2026-07-25)

### 목표

수동 assertion 기반 테스트를 표준 테스트 프레임워크로 전환하고,
함수 단위 성능을 반복 측정할 수 있는 기반을 만듭니다.

### 작업

- GoogleTest와 Google Benchmark를 CMake `FetchContent`로 추가
- 외부 라이브러리 버전을 고정해 개발 환경과 CI의 차이 제거
- `RSS_EXPECT`와 테스트별 수동 `main()` 제거
- core 테스트와 Linux network 테스트를 분리
- CMake `gtest_discover_tests`로 개별 테스트 자동 등록
- packet codec, latency 통계, 방 fanout microbenchmark 추가
- 기존 TCP 부하 테스트 클라이언트 유지
- CI에서 단위 테스트와 microbenchmark 실행 가능 여부 검사
- 테스트와 benchmark 실행 방법 문서화

### 완료 조건

- 기존 테스트 동작이 GoogleTest test case로 모두 이전됨
- `TestSupport.h`가 제거됨
- `ctest --output-on-failure`에서 모든 테스트가 개별 항목으로 표시됨
- Linux network 테스트가 Linux에서만 빌드됨
- `RSS_BUILD_BENCHMARKS=ON`일 때 microbenchmark가 빌드됨
- Release와 ASan/UBSan 구성에서 전체 테스트 통과
- CI에서 build, test, format, clang-tidy 검사 통과

### 작업 브랜치

```text
test/google-test-benchmark
```

## 2주차: 과부하 제어

> 상태: 진행 중 (pull request 병합 전)

### 목표

요청이나 응답이 처리 속도보다 빠르게 쌓여도 메모리가 제한 없이
증가하지 않도록 서버의 동작 기준을 정합니다.

### 작업

- 입력 queue와 출력 queue에 최대 크기 설정
- 세션별 pending write byte 제한
- 입력 queue high watermark에서 읽기와 accept를 일시정지하고 low
  watermark에서 재개
- 출력 queue 포화가 worker를 거쳐 입력 흐름으로 역전파되도록 처리
- 같은 세션의 패킷과 연결 종료 이벤트 처리 순서 보장
- 입력 이벤트별 출력 메시지 수와 총 byte 제한
- 느린 클라이언트의 세션만 연결 종료
- 읽기 일시정지·재개, queue 포화, 느린 세션 종료, 연결 거절 통계 추가
- 정상 상태와 과부하 상태를 구분하는 GoogleTest 추가
- 제한값을 `ServerConfig`에서 조정할 수 있도록 구성
- README와 서버 구조 문서에 과부하 동작 설명

### 완료 조건

- 입력·출력 queue와 세션 전송 대기열이 모두 제한된 크기를 가짐
- 제한에 도달했을 때의 동작이 코드와 문서에서 일치함
- 느린 클라이언트 하나가 다른 세션의 처리를 막지 않음
- 과부하 관련 단위 테스트와 통합 테스트 통과
- 기존 정상 요청 테스트에 회귀 없음

### 작업 브랜치

```text
feat/backpressure
```

## 3주차: 실제 서버 시나리오 측정

### 목표

단순 `PING` 측정을 넘어 방 broadcast와 과부하 상황을 재현하고, 같은
환경에서 반복할 수 있는 측정 절차를 만듭니다.

### 작업

- 한 방에 여러 사용자가 있는 broadcast 시나리오 추가
- 여러 방이 동시에 동작하는 시나리오 추가
- 응답을 늦게 읽는 slow-client 시나리오 추가
- queue saturation과 연결 종료 횟수 집계
- 테스트 환경, 실행 인자, Git commit을 결과와 함께 기록
- warm-up 후 같은 설정을 여러 번 반복하는 실행 명령 제공
- 처리량과 p50/p95/p99 latency를 함께 출력
- 결과 해석 시 환경 차이와 오차 범위 기록

### 완료 조건

- 모든 시나리오를 한 명령으로 반복 실행할 수 있음
- 실패한 연결과 누락된 메시지를 성능 수치에서 숨기지 않음
- 측정 결과에 CPU, 운영체제, compiler, build type, worker 수가 포함됨
- 서로 다른 commit의 결과를 같은 조건에서 비교할 수 있음
- `docs/benchmark.md`의 명령과 실제 도구 사용법이 일치함

### 작업 브랜치

```text
perf/load-scenarios
```

## 공통 작업 규칙

- 하나의 pull request에는 한 주차의 목표만 포함합니다.
- 구현되지 않은 기능이나 측정하지 않은 수치를 문서에 기록하지 않습니다.
- 성능 변경 전후에는 같은 환경과 같은 실행 인자를 사용합니다.
- 기능 변경에는 해당 동작을 검증하는 테스트를 함께 추가합니다.
- `format-check`, `tidy-check`, build, test가 모두 통과한 뒤
  pull request를 생성합니다.
