# 외부 부하 클라이언트 종료 오류의 패킷 분석과 수정

원래 다중 방 부하 조건에서 클라이언트 FIN 뒤 서버의 퇴장 알림이 도착하고,
클라이언트 RST와 서버 EPIPE가 이어지는 경로를 재현했습니다. 부하 실행기의
완전 close를 송신 half-close와 EOF 수신으로 바꿨습니다. 서버의 오류 분류,
종료 코드와 카운터는 변경하지 않았습니다.

## 환경과 재현 조건

- 변경 전 소스: `30541e1522b2df12f03775de3b9c8bd31a3d1343`
- 변경 후: 위 소스에 이 문서와 같은 커밋의 클라이언트 정리 수정 적용
- Ubuntu 24.04.4 arm64, Linux 5.15.49-linuxkit-pr, GCC 13.3.0 Release
- 실행 컨테이너: CPU quota 4개, 메모리 4 GiB
- 서버 CPU 0–1, worker 2개; 부하 생성기 CPU 2–3, 별도 프로세스
- 별도 PostgreSQL 16.15: CPU 0–1, quota 1개, 메모리 1 GiB; 초기 임시 DB
- 20명, 4개 방, client별 300건, payload 256바이트, rate 10건/초,
  max-in-flight 8, timeout 60초; warm-up 1회 뒤 측정 3회
- 각 조건에서 서버를 새로 시작하고 4회 동안 유지, 10초 주기 NDJSON 및
  SIGTERM 종료 통계 수집. 테스트나 빌드를 측정과 동시에 실행하지 않음

[외부 실행 절차](../../benchmark.md#외부-서버와-분리-실행)에 따라 DB와
서버 환경 변수를 준비한 뒤 다음 명령을 사용했습니다. DB 접속 문자열은
기록하지 않습니다. 각 프로세스의 출력은 별도 파일에 저장합니다.

```bash
# 서버 시작 전에 별도 터미널에서 캡처한다.
taskset -c 2,3 tcpdump -i lo -nn -s 0 -U -w traffic.pcap 'tcp port 19090'
taskset -c 0,1 /build/release/rss_server 127.0.0.1 19090 2
# 서버의 server_started 로그를 확인한 뒤 실행한다.
taskset -c 2,3 /build/release/rss_load_scenario_runner \
  --host 127.0.0.1 --port 19090 --scenario multi-room \
  --clients 20 --rooms 4 --messages 300 --payload-bytes 256 --repeat 3 \
  --rate-per-client 10 --max-in-flight 8 --timeout-seconds 60
```

`packets-only-1`은 클라이언트 코드를 계측하지 않고 tcpdump만 사용했습니다.
`close-probe-1`은 추가로 LD_PRELOAD의 close wrapper에서 연결된 대상 포트가
19090인 소켓의 `FIONREAD`, `recv(MSG_PEEK | MSG_DONTWAIT)` 최대 4096바이트,
실제 close 전후 시각과 로컬 포트를 stderr에 기록했습니다. 의도적인 sleep은
없지만 추가 시스템 호출과 출력이 타이밍을 바꾸므로 별도 관측입니다.
`fixed-*`는 각각 같은 관측 방법을 수정 후 반복한 결과입니다. EOF 수신 후에는
getpeername이 실패해 wrapper가 기록하지 않을 수 있으므로, probe 출력의
부재를 정상 종료의 증거로 쓰지 않습니다.

원시 출력·NDJSON·명령·실행 파일 및 전체 캡처 SHA-256은
[results.json](results.json)에 있습니다. 소스 mount 환경의 실행기 환경 줄은
`commit=unknown`이므로 위 소스 상태와 실행 파일 해시를 함께 사용합니다.
네 실행의 서버 실행 파일 해시는 동일합니다.

## 직접 관측한 원인

`packets-only-1`의 warm-up 연결 정리 중 session 20에서 다음 순서가 나왔습니다.
시각은 Unix 초이며 캡처 해상도는 마이크로초입니다.

| 시각 | 방향 | 관측 |
| --- | --- | --- |
| 1788843707.046312 | client 59992 → server 19090 | FIN, ACK |
| 1788843707.046689 | server → client | 118바이트 RoomBroadcast, `event=LEAVE`, room 4, session 4의 퇴장 |
| 1788843707.046714 | client → server | RST |
| 1788843707.046 (밀리초 로그) | 서버 NDJSON | session 20, `epoll_error`, errno 32(EPIPE), events 8221, pending write 358바이트 |

FIN 뒤 377µs에 퇴장 알림, 그 뒤 25µs에 RST가 나타났습니다. NDJSON은
밀리초 해상도이므로 같은 밀리초 안의 정확한 순서를 별도로 추정하지 않습니다.
[추출 패킷](session-20-close.pcap)은 해당 연결의 마지막 약 100ms를 보존하며,
[해석한 증거](evidence.json)에는 FIN/RST, payload 원문·hex, SYN 연결 순서와
오류 이벤트를 담았습니다. 실행기가 연결·협상·로그인을 순차 완료하고 이 서버에
별도 준비 연결을 만들지 않았으므로 20번째 연결과 session 20을 대응했습니다.
이름과 UUID는 임시 부하에서 생성한 값입니다.

기존 실행기는 송수신 스레드가 모두 끝나면 외부 연결들을 완전히 닫았습니다.
먼저 닫힌 연결의 퇴장 처리가 같은 방의 다른 연결에 broadcast를 생성하며,
이미 닫힌 peer에 도착한 알림이 RST를 만들었습니다. 추가 close 관측에서는
80개 중 6개 close 직전에도 119–120바이트의 읽지 않은 LEAVE가 보였습니다.
따라서 모든 측정 채팅 수신을 마친 것과 TCP 수신 정리가 끝난 것은 달랐습니다.
서버가 관측한 소켓 오류를 정상 EOF로 바꾸면 이 사실을 숨기게 됩니다.

## 수정과 전후 결과

정상 송수신을 마친 외부 연결에 `shutdown(SHUT_WR)`을 호출하고 EOF까지
읽은 뒤 fd를 닫습니다. 종료 중 도착한 데이터는 측정 표본에 넣지 않습니다.
전체 연결에 공통 2초 deadline을 적용하며 정리 오류·timeout은
`client_cleanup_*`와 `failed_clients`에 기록합니다. 이미 송수신에 실패한
연결은 즉시 닫고 중복 실패를 세지 않습니다. 내장 서버의 종료 절차는 유지합니다.

| 조건 | 서버 peer_closed / socket_error | 캡처 RST | 측정 성공 |
| --- | ---: | ---: | ---: |
| 수정 전, 패킷 캡처 | 79 / 1 | 1 | 3/3 |
| 수정 전, 패킷 + close 관측 | 62 / 18 | 11 | 3/3 |
| 수정 후, 패킷 캡처 | 80 / 0 | 0 | 3/3 |
| 수정 후, 패킷 + close 관측 | 80 / 0 | 0 (캡처 drop 433건) | 3/3 |

매회 송신 6,000건·수신 30,000건이며 누락·중복·예상 밖 수신·클라이언트 실패는
모두 0입니다. 수정 후 cleanup 실패도 0입니다. 서버·클라이언트 종료 코드는
네 조건 모두 0입니다. 캡처의 kernel drop은 앞의 세 조건에서 0건,
`fixed-probe-1`에서 433건입니다. 마지막 조건의 캡처된 RST는 0건이지만
패킷 누락 때문에 실제 RST 부재를 입증하지는 못합니다. 해당 조건의 서버
소켓 오류 0건은 별도 NDJSON으로 확인했습니다. 서버 수치는 warm-up,
준비, 측정 3회와 정리를 포함한 누적 값이며 run별 값이 아닙니다.
`server_stats=unavailable`은 미수집을 뜻하고, 위 수치는 별도 서버 NDJSON에서
가져왔습니다. 패킷 RST 수와 서버 소켓 오류 수는 집계 대상이 달라 일대일
대응한다고 가정하지 않습니다.

## 회귀 검증

- 클라이언트 FIN을 확인한 peer가 늦은 broadcast를 전송하고 EOF를 보내는
  테스트: 데이터를 모두 읽고 fd를 닫는지 검증
- EOF가 오지 않는 peer: deadline 실패와 fd 해제 검증
- 수신 ECONNRESET: socket_error 보존과 fd 해제 검증
- 채팅 수신은 성공했지만 정리에 응답하지 않는 외부 peer: cleanup timeout,
  실패 판정 및 측정 시간과 정리 시간의 분리 검증
- 보고서: 메시지가 모두 도착했더라도 cleanup 실패가 성공으로 표시되지 않음

기존 완전 close를 유지한 구현에서 위 정리 테스트 실패를 확인한 뒤 수정했습니다.
새 보고 항목과 실행기 실패 판정도 각각 수정 전 실패를 확인했습니다.
전체 빌드·테스트와 정적 검사 결과는 [validation.json](validation.json)에 기록합니다.

## 남은 한계와 다음 작업

과거 [최초 측정](../2026-09-08-external-target/README.md)의 1건에는 errno와
패킷이 없으므로 그것의 정확한 발생 시각·errno는 소급 확정할 수 없습니다.
이번에는 같은 부하 조건에서 종료 오류 경로를 실제로 재현하고 수정했습니다.
추적이 타이밍에 영향을 주므로 발생 빈도나 장기 무오류를 입증하지 않습니다.
서로 다른 관측 방법 사이의 오류 건수를 성능 비교로 사용하지 않습니다.

수정 후 패킷 캡처 조건의 p99도 41.803–42.070ms로 남았습니다. 정리 수정은
측정 종료 이후의 동작이며 지연 개선으로 해석하지 않습니다. 다음은 p99 약
41–69ms의 TCP 송수신·스케줄링 원인과 RoomService mutex 경합을 각각 측정하는
것입니다. 서로 다른 물리 머신의 네트워크나 연결 수가 훨씬 큰 환경에서는
전체 2초 정리 한도를 재검증해야 합니다.
