# 1차 버전 데모

이 데모는 로그인, 방 생성·참가, 양방향 채팅, 퇴장과 연결 종료를 확인합니다.
서버는 Linux에서 실행하고 Qt 클라이언트는 Linux, macOS 또는 Windows에서
실행합니다. 설치와 빌드는 [README](../README.md#빠른-시작)를 따릅니다.

## 준비

1. README의 Docker Compose 절차로 PostgreSQL과 migration을 준비합니다.
   `postgres`가 healthy이고 `migrate` 종료 코드가 0인지 확인합니다.
2. RSS_DATABASE_URL을 지정하고 서버를 실행합니다.
   기본 포트는 7777입니다. 다른 머신의 클라이언트는 실제 서버 주소를 사용합니다.
3. Qt 클라이언트 하나와 콘솔 클라이언트 하나를 실행합니다. Qt 두 개로도
   같은 과정을 진행할 수 있습니다.

## 두 사용자로 채팅

| 순서 | Qt 클라이언트 | 콘솔 클라이언트 | 확인할 결과 |
| --- | --- | --- | --- |
| 1 | 서버 주소·포트 입력 후 연결 | `rss_console_client <주소> <포트>` 실행 | 두 연결의 버전 협상 성공 |
| 2 | `demo-alice`로 로그인 | `/login demo-bob` | 사용자 ID와 로그인 상태 표시 |
| 3 | `demo-room` 생성 | 반환된 방 번호로 `/join <방 번호>` | 같은 방에 입장하고 참가 알림 수신 |
| 4 | `안녕하세요` 전송 | 채팅 수신 확인 후 `/chat 반갑습니다` | 양쪽에서 두 메시지 확인 |
| 5 | 콘솔 사용자의 퇴장 알림 확인 | `/leave` | 방 상태와 퇴장 알림 일치 |
| 6 | 방 나가기 후 연결 종료 | `/quit` | 연결·방 UI 상태 초기화 |

방 번호는 매번 서버가 반환한 값을 사용합니다. 같은 로그인 이름으로 다시
연결하면 DB가 유지되는 동안 같은 사용자 UUID를 받습니다. 이름 로그인은
인증 기능이 아니며 방과 채팅은 서버 재시작 시 복구되지 않습니다.

콘솔에서 `/pos 10.5 22.0`, `/ping`도 실행할 수 있습니다. 현재 Qt 화면의
위치 입력·시각화나 PING 지연 표시를 구현한 것으로 해석하지 않습니다.

## 종료

서버에 Ctrl+C 또는 SIGTERM을 보내 정상 종료를 확인합니다. 로그의 최종
snapshot은 모든 준비·연결 정리를 포함한 누적 통계입니다.

DB를 보존하려면 `docker compose down`을 사용합니다. 본인이 데모용으로 만든
임시 DB만 삭제할 때는 같은 Compose 프로젝트에서 `docker compose down -v`를
실행합니다.

자동화 검증 범위는 [프로젝트 상태](project-status.md)와
[테스트 가이드](../CONTRIBUTING.md)를 참고합니다. 성능 수치와 한계는
[벤치마크 가이드](benchmark.md)에서 별도로 확인합니다.

## 실행 확인 기록

2026-09-08에 README의 Compose DB·migration을 준비하고 Linux Release 서버와
macOS Qt 6.11.1, Linux 콘솔로 위 절차를 확인했습니다. 한글 양방향 채팅,
위치·PING, 퇴장 알림, Qt 입력 상태 초기화와 재접속 UUID 유지가 동작했습니다.
최종 서버 통계는 정상 peer 종료 3건, socket_error 0, 남은 세션 0입니다.
환경·콘솔 출력·서버 통계는
[데모 검증 기록](performance/2026-09-08-tcp-nodelay/demo-validation.json)에 있습니다.
