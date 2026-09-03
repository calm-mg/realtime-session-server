# 프로젝트 상태

이 문서는 현재 기본 브랜치의 구현 상태와 다음 작업의 우선순위를 요약합니다.
세부 결함은 [알려진 문제](known-issues.md), 중장기 방향은
[로드맵](roadmap.md), 서버의 현재 동작은 [서버 구조](architecture.md)를
참고합니다.

마지막 갱신: 2026-09-03

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
- PostgreSQL 영구 사용자 UUID와 같은 이름 재접속 복구
- bounded DB executor와 deferred handler completion 기반
- NDJSON 운영 로그와 주기·최종 과부하 통계 외부 노출

## 최근 완료

- 2026-09-03: 시작·실패·종료 구조화 로그와 설정 가능한 주기 및 최종
  과부하 snapshot 출력을 추가하고 민감한 사용자·DB 값은 제외
- 2026-08-27: PostgreSQL 사용자 저장소와 영구 UUID 로그인을 운영 서버에
  연결하고 DB 작업 중 worker 비차단 및 실패 세션 격리 적용
- 2026-08-27: worker handler 예외를 실패 세션에 격리하고 연결 종료와
  `handler_exceptions` 통계로 관측
- 2026-08-26: `ServerConfig` 전체 값의 유효 범위 검증과 설정 문서 보강
- 2026-08-25: `SIGINT`와 `SIGTERM`을 기존 정상 종료 drain 경로에 연결
- 2026-08-20: 반복 로그인을 거부하고 명시적 퇴장 뒤에만 방 생성·참가 허용
- 2026-08-20: 동일한 방 재참가를 명시적 오류로 처리하고 기존 방 상태 보존
- 2026-08-20: Qt 전송 오류와 연결 상태를 일치시키고 실패한 채팅 입력 보존
- 2026-08-20: 실제 서버 시나리오의 deadline 회귀 테스트를 성공한 전송에
  기반한 결정적 조건으로 변경하고 sanitizer 반복 실행 안정성 확보

## 현재 단계

기본 기능과 과부하 제어, 영구 사용자 ID, handler 예외 격리, 운영 종료와
구조화된 관측 경로를 갖췄습니다. 다음 단계에서는 여러 방 참가 모델과 채팅
기록 영속화에 앞서 프로토콜과 클라이언트의 데이터·메모리 계약을 보강합니다.

### 우선순위 1: 프로토콜과 클라이언트 견고성

- 문자열 payload의 구분자와 UTF-8 처리 규칙 정의
- Qt 요청 진행 상태와 중복 요청 방지
- Qt 채팅 로그와 송신 대기 byte 상한 추가

### 우선순위 2: 영속성과 측정 기반 확장

- PostgreSQL 영구 방과 사용자-방 다대다 membership 설계
- 방별 채팅 기록, 읽음 cursor와 ScyllaDB/Cassandra adapter 설계
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

- 완료 여부와 지금 착수할 작업의 순서는 이 문서에서만 관리합니다.
- 재현 가능한 열린 결함과 수정 완료 조건은 `known-issues.md`에서
  관리하며, 해결 이력과 우선순위를 중복해서 기록하지 않습니다.
- 아직 착수하지 않은 중장기 기능과 성능 방향은 `roadmap.md`에서
  관리하며, 현재 작업의 상세 목록을 중복해서 기록하지 않습니다.
- `development-plan.md`는 완료된 초기 개발 계획의 기록입니다.
- `design/`과 `plans/`는 각각 당시 결정과 구현 절차의 기록으로 유지합니다.
