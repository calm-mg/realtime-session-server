# 외부 다중 방 소켓 오류의 동일 조건 재현 조사

기존 [외부 대상 측정](../2026-09-08-external-target/README.md)의
`disconnect_socket_error=1`을 조사했습니다. 원래 부하 인자와 프로세스·CPU
배정을 유지하고, 변경 전 빌드 및 오류 순간만 기록하는 빌드를 실행했습니다.
기존 1건의 errno·발생 시점은 여전히 알 수 없습니다. 종료 분류·카운터를
바꾸거나 지연 개선을 적용하지 않았습니다.

## 재현 환경

- 기반 소스: `2d684cc309a3ccf50a206c870d496b329d677c82` (미머지 선행 변경)
- Ubuntu 24.04.4 LTS arm64, Linux `5.15.49-linuxkit-pr`
- GCC `13.3.0-6ubuntu2~24.04.1`, Release, sanitizer 비활성
- 부하 실행 컨테이너: CPU quota 4개, 메모리 4 GiB
- 서버: CPU 0–1, worker 2개. 생성기: CPU 2–3. 같은 VM의 loopback TCP
- 전용 PostgreSQL 16.15: 별도 컨테이너, CPU 0–1, quota 1개, 메모리 1 GiB
- 임시 DB에 `001_users.sql` 적용. 로그인만 영속화하며 채팅은 저장하지 않음
- 서버 통계 주기 10초. 세트마다 서버를 새로 시작하고 warm-up 1회·측정 3회
- 모든 세트에서 clients 20, rooms 4, messages 300, payload 256,
  rate-per-client 10, max-in-flight 8, timeout-seconds 60 유지
- `strace`, 패킷 추적, sanitizer를 부하 실행에 사용하지 않음. 빌드와 테스트는
  부하 실행 전후에 수행. 다른 호스트 작업과 VM 스케줄링은 완전히 통제하지 못함

변경 전 서버 SHA-256은 기존 측정의
`8502f678e35f135b004fa2c14d083fb4c3d3665babf0823e3fc2985a4b326495`와
일치했습니다. 컨테이너에는 소스 작업 디렉터리만 마운트해 실행기의 commit
표시는 `unknown`입니다. 기반 commit은 호스트 Git에서 별도로 확인했습니다.
각 바이너리 hash와 소스 상태, 계측 코드 diff의 SHA-256, 명령, 시작·종료
시각, 원시 stdout/stderr는 [results.json](results.json)에 보관합니다.

## 관측 결과

| 빌드·세트 | 측정 반복 | 매회 송신 / 수신 | 최종 peer_closed | 최종 socket_error | p99(ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 변경 전 | 3 | 6,000 / 30,000 | 80 | 0 | 44.087–48.611 |
| 오류 계측 1 | 3 | 6,000 / 30,000 | 80 | 0 | 46.902–48.295 |
| 오류 계측 2 | 3 | 6,000 / 30,000 | 80 | 0 | 48.118–48.859 |
| 오류 계측 3 | 3 | 6,000 / 30,000 | 80 | 0 | 44.579–48.230 |

측정 12회 모두 누락·중복·예상 밖 수신·클라이언트 실패가 0이고 서버와
실행기 종료 코드도 0입니다. 각 서버의 최종 세션·입력 queue·출력 queue는
0입니다. 서버 표준 오류는 네 세트 모두 비어 있습니다.

최종 종료 320건은 warm-up 4회와 측정 12회의 각 client 20명에 대응합니다.
원래와 같은 부하에서도 이번 세션에서는 재현되지 않았습니다. 이 결과를
`socket_error` 수정 성공이나 기존 관측 오류로 해석하지 않습니다. 오류가
발생하지 않아 실제 errno·정확한 발생 시각·상대 close의 인과관계는 확보하지
못했습니다. p99는 참고값이며 계측 유무의 성능 차이를 주장하지 않습니다.

## 실행 절차

Release 빌드와 DB migration을 마친 Linux 환경에서 `RSS_DATABASE_URL`을
전용 임시 DB로 설정합니다. 다음 서버의 `server_started` 로그를 확인한 뒤
부하 생성기를 실행했습니다. 준비 확인용 TCP 연결은 만들지 않았습니다.

```bash
RSS_OBSERVABILITY_INTERVAL_SECONDS=10 taskset -c 0,1 \
  /build/base/rss_server 127.0.0.1 19090 2 \
  >server.ndjson 2>server.stderr &
server_pid=$!

taskset -c 2,3 /build/base/rss_load_scenario_runner \
  --host 127.0.0.1 --port 19090 --scenario multi-room \
  --clients 20 --rooms 4 --messages 300 --payload-bytes 256 \
  --repeat 3 --rate-per-client 10 --max-in-flight 8 --timeout-seconds 60 \
  >client.stdout 2>client.stderr
client_exit_code=$?
kill -TERM "$server_pid"
wait "$server_pid"
server_exit_code=$?
```

## 계측 내용과 해석 한계

`TcpServer`의 기존 네 가지 오류 종료 지점에 표준 오류 NDJSON 진단을
추가했습니다. `recv`·`send` 실패의 errno를 먼저 인자로 보존하고,
`EPOLLERR`에서는 `SO_ERROR`를 읽습니다. 조회 실패 자체도 별도 operation으로
기록하며 `send`의 0 반환에는 과거 errno를 대입하지 않습니다.

진단에는 Unix 밀리초 시각, 서버 수명 내 세션 ID, fd, operation, error_code,
epoll_events, 사용자 공간 대기 송신 byte만 포함합니다. 사용자 이름, 방 이름,
채팅 본문, DB 접속 정보는 제외합니다. 필드 의미는
[실패 원인 해석](../../benchmark.md#실패-원인-해석)을 참고합니다.

`SO_ERROR` 조회는 pending error를 읽고 지웁니다. 이 조회는 이미 기존 코드가
소켓 오류로 종료하기로 결정한 경로에만 추가했으며, 이후 같은 종료 경로와
카운터 갱신을 유지합니다. [Linux socket 매뉴얼](https://man7.org/linux/man-pages/man7/socket.7.html)

실제 TCP 연결에 `SO_LINGER={1,0}`을 적용한 reset 테스트로 `epoll_error`와
`ECONNRESET` 진단 1줄, 기존 socket error 1건·peer close 0건을 검증합니다.
이 테스트는 계측 누락을 검출하기 위한 것이며 원래 부하의 재현이 아닙니다.
표준 오류 출력은 동기식이므로 오류 발생 이후의 타이밍에는 영향을 줄 수 있습니다.

원래 관측의 클라이언트 종료와 퇴장 알림 충돌 가설은 미확정입니다.
클라이언트는 측정 reader들을 join한 뒤 객체 정리 시 `close()`하고, 서버는
연결 해제 때 같은 방에 알림을 만들지만, 코드에 이 경로가 있다는 사실만으로
기존 오류를 설명했다고 볼 수 없습니다. 다음 오류가 잡히면 세션 ID로
warm-up/반복 범위를 구분하고, errno와 발생 시각을 기준으로 해당 연결의
클라이언트 close 및 패킷 흐름을 좁혀 추적해야 합니다.

`server_stats=unavailable`은 미수집이며 0이 아닙니다. 서버 최종 통계는
준비·warm-up·모든 측정 반복과 연결 정리를 포함하는 서버 수명 전체의 누적값입니다.
미재현 횟수는 결함 해결이나 장시간 안정성을 입증하지 않습니다. 이 조사에서는
최대 성능을 판단하지 않으며 p99·`RoomService` mutex 분석은 이후 작업입니다.

## 검증 결과

- 변경 전 Linux Release: 282개 통과
- 변경 후 Linux Release: 직렬 전체 283개 통과
- macOS `core-dev`: build 및 174개 테스트 통과
- `format-check`, Clang 18 `tidy-check`: 통과
- TCP reset 로그 테스트: 기존 코드에서 진단 누락으로 실패한 뒤 계측 코드에서 통과
- Linux arm64 Debug ASan/UBSan: 직렬 276개 중 274개 통과, 아래 2개 실패

`ScenarioRunnerTest.DefaultSlowClientLimitDisconnectsSlowClientWithoutFastErrors`는
30초 제한 시간에 걸렸고,
`ScenarioRunnerTest.SharedWindowKeepsFastClientsWithinPendingCapacity`는
느린 client 격리·송수신 수 검증에 실패했습니다. 별도 추출한 변경 전 커밋도
같은 컨테이너·컴파일러·Debug sanitizer 옵션에서 이 두 테스트가 실패했습니다.
ASan/UBSan의 메모리 오류 보고는 없었지만, 이 사실을 sanitizer 전체 성공으로
표현하지 않습니다. 플랫폼/빌드 조건에 따른 시나리오 실패 원인은 별도 확인이
필요하며 이번 오류 계측 변경에서 테스트 상한이나 성공 조건을 바꾸지 않았습니다.

최초 Release 병렬 실행에서는
`ProgramTest.ActualClientSetupFailurePrintsRunRowAndReturnsOne`의
`rejected_connections=1` assertion이 한 번 실패했습니다. 같은 테스트의 단독
100회 반복 및 이후 직렬 전체 실행은 통과했습니다. sanitizer 병렬 실행에서는
위 느린 client 2개 외에 `ScenarioRunnerTest.ClientSetupFailureReturnsMeasurementFailureResult`의
같은 종류의 snapshot assertion도 실패했습니다. 병렬 실패를 직렬 성공으로
덮어쓰지 않고 [검증 요약과 실패 출력](validation.json)에 함께 보관합니다.

검증 명령의 Linux build 디렉터리는 `/build/base`, `/build/sanitizers`이며
sanitizer 옵션은 `-fsanitize=address,undefined -fno-omit-frame-pointer`,
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`,
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`입니다.
