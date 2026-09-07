# 프로젝트 상태

이 문서는 현재 기본 브랜치의 구현 상태와 다음 작업의 우선순위를 요약합니다.
세부 결함은 [알려진 문제](known-issues.md), 중장기 방향은
[로드맵](roadmap.md), 서버의 현재 동작은 [서버 구조](architecture.md)를
참고합니다.

마지막 갱신: 2026-09-07

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
- 연결별 프로토콜 버전 협상과 구버전 연결 거절
- 공용 structured payload codec과 RFC 3629 UTF-8 검증
- 이름·방 32바이트와 채팅 1291바이트 고정 상한

## 최근 완료

- 2026-09-07: 서버·Qt·콘솔·부하 도구에 연결별 버전 협상과 클라이언트의
  5초 협상 제한 시간을 추가하고, 실패 시 오류 송신 후 연결 종료 적용

- 2026-09-07: Qt 로그를 최근 1,000개로 제한하고 소켓 내부 버퍼를 포함한
  송신 대기 상한 1 MiB 및 초과 시 복구 가능한 전송 오류 적용
- 2026-09-04: Qt 클라이언트에 로그인·방 작업 요청의 진행 상태를 추가해
  응답 대기 중 중복 요청과 채팅 전송을 차단하고 관련 입력을 비활성화
- 2026-09-03: 구조화 문자열 value의 percent encoding, UTF-8/control 검증과
  고정 byte 상한을 protocol·server·Qt·콘솔·부하 도구에 함께 적용
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

기본 기능, 과부하 제어, 영구 사용자 ID, 정상 종료, 운영 관측, 문자열 wire
계약과 버전 협상, Qt 요청 상태 및 메모리 상한을 갖췄습니다. 다음 단계는
기능 확대에 앞서 재현 가능한 성능 측정과 병목 분석으로 현재 설계를 검증하는
것입니다.

### 우선순위 1: 성능 측정과 결과 정리

- 완료된 부하 시나리오로 commit별 회귀 기준값 축적
- 같은 환경에서 반복 실행한 처리량, p95/p99, 실패·누락 수 기록
- 정상·과부하·느린 클라이언트 시나리오 비교
- `RoomService` 단일 mutex 경합과 주요 병목 측정
- 측정 결과에 따라 개선 전후 비교 또는 현재 구조 유지 근거 정리
- 실행 환경과 재현 명령, 설계 선택, 측정 한계를 문서에서 연결

### 후속 확장

- PostgreSQL 영구 방과 사용자-방 다대다 membership 설계
- 방별 채팅 기록, 읽음 cursor와 ScyllaDB/Cassandra adapter 설계
- 반복 연결, 혼합 workload와 원격 환경 측정 범위 검토
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
