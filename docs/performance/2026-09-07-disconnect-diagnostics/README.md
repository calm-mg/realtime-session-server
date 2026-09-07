# 큰 burst의 연결 종료 원인과 정리 누락

[예비 측정](../2026-09-07-baseline/README.md)에서 관측한 20명·worker 2의
연결 실패를 다시 실행하고, 기존 카운터로 구분하지 못했던 원인을 계측했습니다.
이번 실험은 실패 원인 확인이며 처리량 개선 비교가 아닙니다.

## 재현과 원인

동일한 Apple M1 Pro의 Linux arm64 컨테이너, CPU quota 4개, 메모리 4 GiB,
GCC 13.3.0 Release 환경에서 다음 조건을 사용했습니다.

```bash
rss_load_scenario_runner --scenario broadcast --clients 20 --messages 100 \
  --payload-bytes 256 --workers 2 --repeat 1
```

변경 전 `a598180`은 20개 클라이언트 실패와 38,464건 누락을 보고했지만
기존 거절·종료·예외 카운터는 0이었습니다. [before.json](before.json)은
Git archive로 빌드해 실행기의 commit이 `unknown`이며, 원본 commit은
파일의 `source_commit`에 별도로 기록했습니다.

종료 정책을 바꾸지 않고 계측만 추가한 결과는 다음과 같습니다.

| 관측값 | 수 |
| --- | ---: |
| `worker_parked_limit_failures` | 20 |
| `disconnect_worker_requested` | 20 |
| `client_receive_peer_closed` | 20 |
| `worker_invalid_sequence_failures` | 0 |
| `disconnect_socket_error`, `disconnect_protocol_error` | 0 |

즉, 세션별 대기 이벤트 상한 32개 초과로 worker가 세션을 실패 처리하고,
I/O 스레드가 종료 명령을 처리한 뒤 클라이언트가 EOF를 받았습니다.
[instrumented.json](instrumented.json)은 이 중간 버전의 실행 파일 해시와
명령·출력을 보관합니다. 실행기의 commit은 구성 시점의 HEAD이며 미커밋
계측 변경을 포함한다는 사실을 `source` 필드에 명시했습니다.

## 함께 발견한 결함

거절된 이벤트의 순서 번호가 빠진 뒤 `Disconnected` 이벤트가 도착하면,
기존 worker가 빠진 번호를 계속 기다리거나 종료 이벤트까지 대기 상한으로
거절했습니다. 이 때문에 실제 소켓은 닫혀도 `RoomService`의 사용자와 방
참가 상태, 프로토콜 협상 상태가 정리되지 않았습니다.

수정은 실패한 세션의 종료 이벤트를 별도 단일 슬롯에 보존하고, 실행 중인
handler나 deferred completion이 끝나면 순서 번호의 빈 구간과 관계없이
정리하도록 합니다. 정상 세션의 이벤트 순서와 세션별 handler 직렬 실행은
유지합니다. 먼저 꺼냈지만 늦게 실행되는 이벤트도 같은 종료 상태를 보도록
상태 참조를 확보해, 정리 후 상태가 다시 만들어지는 경합도 차단합니다.

큰 burst의 상한 초과 자체는 계속 종료 대상입니다. 입력 제한이나 worker
기본값을 높여 실패를 숨기지 않았습니다. 다음 측정은 송신 속도와 대기 메시지
수를 제어하는 지속 부하에서 정상 처리 범위와 병목을 확인하는 것입니다.

## 최종 재실행

최종 소스 commit, 실행 파일 해시와 모든 재실행 원시 출력은 `after.json`에
기록합니다. 실패한 burst의 일부 수신 처리량은 정상 처리량으로 인용하지
않습니다. 정상·느린 클라이언트 결과와 종료 이유를 함께 확인합니다.
