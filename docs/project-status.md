# 프로젝트 상태

이 문서는 현재 기본 브랜치의 구현 상태와 다음 작업의 우선순위를 요약합니다.
세부 결함은 [알려진 문제](known-issues.md), 중장기 방향은
[로드맵](roadmap.md), 서버의 현재 동작은 [서버 구조](architecture.md)를
참고합니다.

마지막 갱신: 2026-08-20

## 완료된 기반

- C++20 기반 프로토콜, 서버 코어와 Linux 네트워크 계층 분리
- `epoll`과 `eventfd`를 사용하는 단일 I/O 스레드 서버
- worker pool과 세션별 이벤트 순서 보장
- 로그인, 방 생성·참가·퇴장, 채팅과 위치 broadcast
- 입력·출력 queue, 세션 pending write와 동시 세션 수 제한
- high/low watermark 기반 읽기 흐름 제어와 느린 클라이언트 격리
- 정상 종료 drain 상태 머신과 과부하 통계
- 실제 서버의 broadcast, 다중 방과 slow-client 부하 시나리오 실행기
- 실행 환경, 실패·누락 수와 p50/p95/p99를 포함하는 반복 측정 결과
- GoogleTest, Google Benchmark, ASan/UBSan과 플랫폼별 CI 기반
- Qt 6 Widgets 클라이언트의 application, network, UI 모듈 분리
- Qt 클라이언트의 연결, 로그인, 방 작업과 채팅 UI

## 현재 단계

기본 기능과 과부하 제어 기반은 갖춰졌습니다. 다음 단계에서는 기능을 더
늘리기 전에 알려진 상태 일관성 문제와 운영 종료 경로를 먼저 수정합니다.

### 우선순위 1: 정확성

- 동일한 방에 다시 참가할 때 방이 Lobby에서 사라지는 문제 수정
- Qt 전송 오류 후 Controller와 실제 소켓 상태 일치
- 전송에 실패한 채팅 입력 보존
- 방 이동과 반복 로그인에 대한 서버 상태 규칙 정의

### 우선순위 2: 운영 안정성

- `SIGINT`와 `SIGTERM`을 `TcpServer::stop()`에 연결
- `ServerConfig` 전체 값 검증
- worker handler 예외 처리 정책 정의
- 구조화된 로그와 과부하 통계 외부 노출 방법 결정

### 우선순위 3: 프로토콜과 클라이언트 견고성

- 문자열 payload의 구분자와 UTF-8 처리 규칙 정의
- Qt 요청 진행 상태와 중복 요청 방지
- Qt 채팅 로그와 송신 대기 byte 상한 추가

### 우선순위 4: 측정과 확장

- 완료된 부하 시나리오로 commit별 회귀 기준값 축적
- 반복 연결, 혼합 workload와 원격 환경 측정 범위 검토
- `RoomService` 단일 mutex 경합 측정
- 측정 결과에 따라 방 단위 잠금 또는 shard 검토
- 기능 증가 시 `TcpServer` 책임 분리 검토

## 검증 기준

변경은 관련 회귀 테스트를 먼저 추가하고 다음 검증을 통과해야 합니다.

```bash
cmake --preset core-dev
cmake --build --preset core-dev
ctest --preset core-dev
```

Linux 네트워크 변경은 `linux-dev`, Qt 변경은 `qt-client-dev` preset으로
추가 검증합니다. 완료 전 `format-check`와 가능한 경우 `tidy-check`를
실행합니다.

## 문서 사용 규칙

- 완료 여부와 현재 우선순위는 이 문서에서 관리합니다.
- 재현 가능한 결함과 수정 완료 조건은 `known-issues.md`에서 관리합니다.
- 아직 착수하지 않은 기능과 성능 방향은 `roadmap.md`에서 관리합니다.
- `design/`과 `plans/`는 당시 변경의 설계 및 구현 기록으로 유지합니다.
