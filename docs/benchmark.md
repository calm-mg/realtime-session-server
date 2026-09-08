# 벤치마크 실행과 해석

성능 검사는 목적이 다른 세 도구로 나뉩니다.

- `rss_microbenchmarks`: 네트워크 없이 작은 코드 경로의 실행 시간을 측정
- `rss_load_test_client`: 실제 TCP 연결과 `PING`/`PONG` 왕복 시간을 측정
- `rss_load_scenario_runner`: 로컬 실제 서버의 방 broadcast, 다중 방, 느린
  클라이언트 격리와 내부 과부하 통계를 반복 측정

마이크로벤치마크 숫자는 서버의 동시 접속 처리량이 아닙니다. 반대로
TCP 부하 테스트 결과만으로 어느 함수가 느린지는 알 수 없습니다.

첫 측정의 조건과 성공·실패 원시 결과는
[Linux 컨테이너 예비 기준값](performance/2026-09-07-baseline/README.md)에
정리했습니다. 짧은 burst 측정이므로 최대 처리량이나 성능 합격선으로
사용하지 않습니다.

큰 burst의 종료 원인과 함께 발견한 세션 정리 결함은
[연결 종료 진단](performance/2026-09-07-disconnect-diagnostics/README.md)에
후속 재현 결과를 기록했습니다.

송신 속도·방별 응답 대기 상한을 적용한 약 30초 반복 측정은
[지속 부하 기준값](performance/2026-09-07-sustained-load/README.md)에
정상·다중 방·느린 client 결과와 함께 기록했습니다.

운영 서버와 부하 생성기의 프로세스·CPU를 나눈 후속 결과는
[외부 대상 분리 측정](performance/2026-09-08-external-target/README.md)에
서버 로그와 함께 보관했습니다. 소켓 오류 1건의 후속 시도와 최소 계측은
[동일 조건 재현 조사](performance/2026-09-08-socket-error-investigation/README.md)에
기록합니다.

## 마이크로벤치마크

Google Benchmark 기반 실행 파일은 다음 세 코드 경로를 측정합니다.

- 패킷 인코딩과 디코딩
- 지연 시간 표본의 백분위 계산
- 방 인원수에 따른 채팅 메시지 생성과 패킷 인코딩

Release 설정과 `RSS_BUILD_BENCHMARKS` 옵션으로 빌드합니다.

```bash
cmake --preset benchmark
cmake --build --preset benchmark --target rss_microbenchmarks --parallel
./build/benchmark/rss_microbenchmarks
```

특정 항목만 측정할 수도 있습니다.

```bash
./build/benchmark/rss_microbenchmarks \
  --benchmark_filter='MessageRouterFixture/ChatFanout'
```

결과의 `Time`은 실제 경과 시간, `CPU`는 해당 작업에 사용된 CPU
시간입니다. `bytes_per_second`와 `items_per_second`는 한 번의 반복에서
처리했다고 표시한 데이터 양을 기준으로 계산됩니다.

채팅 측정은 준비 단계에서 버전 협상과 실제 broadcast의 수신자·본문을
검증합니다. 검증 실패는 종료 코드 1로 보고합니다. 실패한 반복이 통계에서
숨겨지는 것을 방지하기 위해 `--benchmark_report_aggregates_only`와
`--benchmark_display_aggregates_only`의 활성화는 거절합니다. 같은 이름의
`BENCHMARK_REPORT_AGGREGATES_ONLY`, `BENCHMARK_DISPLAY_AGGREGATES_ONLY`
환경변수도 해제하거나 `false`로 설정해야 합니다. 일반 반복 결과와 JSON
파일에는 개별 값과 집계 값이 함께 기록됩니다.

다른 프로세스, CPU 절전 상태, 가상화 환경에 따라 결과가 달라지므로
한 번의 숫자나 서로 다른 PC의 숫자를 그대로 비교하지 않습니다.

## TCP `PING` 부하 테스트

`rss_load_test_client`는 여러 TCP 클라이언트를 만들고,
각 클라이언트가 `PING`을 반복해서 보낸 뒤 `PONG`이 돌아오는 시간을
측정합니다.

이 도구는 연결과 간단한 요청/응답 성능을 확인하기 위한 것입니다. 채팅
broadcast, 느린 클라이언트, queue 포화 상태는 이 도구가 아니라 아래의
`rss_load_scenario_runner`로 측정합니다.

### 빌드

Release 설정으로 빌드합니다.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### 실행

첫 번째 터미널에서 서버를 실행합니다.

```bash
./build/rss_server 0.0.0.0 7777 4
```

두 번째 터미널에서 부하 테스트를 실행합니다.

```bash
./build/rss_load_test_client 127.0.0.1 7777 100 100
```

인자는 다음 순서입니다.

```text
rss_load_test_client <서버 주소> <포트> <클라이언트 수> <클라이언트별 요청 수>
```

인자를 생략할 때의 기본값은 다음과 같습니다.

| 인자 | 기본값 |
| --- | ---: |
| 서버 주소 | `127.0.0.1` |
| 포트 | `7777` |
| 클라이언트 수 | `100` |
| 클라이언트별 요청 수 | `100` |

### 출력 읽는 방법

다음은 출력 형식을 설명하기 위한 예시입니다. 프로젝트의 실제 성능
측정값이 아닙니다.

```text
clients=100 messages_per_client=100 sent=10000 failed_clients=0 elapsed_sec=1.25 approx_msg_per_sec=8000 latency_samples=10000 min_ms=0.10 p50_ms=0.80 p95_ms=2.40 p99_ms=4.10 max_ms=8.50
```

| 항목 | 의미 |
| --- | --- |
| `clients` | 동시에 연결을 시도한 클라이언트 수 |
| `messages_per_client` | 각 클라이언트가 보낸 `PING` 수 |
| `sent` | 실제로 전송하고 응답까지 받은 요청 수 |
| `failed_clients` | 연결 또는 통신 중 실패한 클라이언트 수 |
| `elapsed_sec` | 전체 테스트에 걸린 시간 |
| `approx_msg_per_sec` | `sent / elapsed_sec`로 계산한 초당 요청 수 |
| `latency_samples` | 응답 시간 표본 수 |
| `min_ms` | 가장 짧은 응답 시간 |
| `p50_ms` | 표본의 50%가 이 값 이하인 응답 시간 |
| `p95_ms` | 표본의 95%가 이 값 이하인 응답 시간 |
| `p99_ms` | 표본의 99%가 이 값 이하인 응답 시간 |
| `max_ms` | 가장 긴 응답 시간 |

예를 들어 `p99_ms=4.10`은 전체 요청의 약 99%가 4.10ms 이내에
응답했다는 뜻입니다.

프로그램은 실패한 클라이언트가 하나라도 있거나 응답 시간 표본이 없으면
0이 아닌 종료 코드를 반환합니다.

## 결과를 기록할 때 필요한 정보

서로 다른 환경의 숫자를 비교하려면 최소한 다음 정보를 함께
기록해야 합니다.

- CPU 모델과 코어 수
- 메모리 용량
- 운영체제와 Linux kernel 버전
- 물리 Linux, WSL2, 가상 머신, 컨테이너 중 어떤 환경인지
- 컴파일러 버전
- Git commit
- Release 또는 Debug 빌드 여부
- 서버 worker 수
- 클라이언트 수와 클라이언트별 요청 수
- 서버와 부하 도구가 같은 PC에서 실행됐는지 여부
- 테스트 반복 횟수

## 측정 순서

1. 다른 빌드나 로그 출력이 성능에 영향을 주지 않도록 Release로
   빌드합니다.
2. 서버를 실행하고 정상적으로 접속 가능한지 확인합니다.
3. 작은 부하를 한 번 보내서 코드와 메모리 페이지를 준비합니다.
4. 같은 설정으로 최소 5번 반복합니다.
5. 각 실행의 `p50`, `p95`, `p99`, 처리량, 실패 수를 보관합니다.
6. 중간값과 실행 간 차이를 함께 확인합니다.
7. 설정을 하나만 바꾼 뒤 다시 같은 횟수로 측정합니다.

## 결과 해석 시 주의사항

- 서버와 부하 도구를 같은 PC에서 실행하면 CPU와 네트워크 자원을 서로
  사용합니다.
- WSL2, Docker, 물리 Linux의 결과를 같은 환경처럼 비교하면 안 됩니다.
- 초당 처리량이 높아도 `p99`가 크게 증가하면 일부 사용자는 느린 응답을
  경험합니다.
- 평균값만 보면 소수의 매우 느린 응답을 발견하기 어렵습니다.
- `failed_clients`가 0이 아닌 결과는 정상 처리량으로 해석하면 안 됩니다.
- 현재 도구의 처리량은 단순히 완료한 요청 수를 전체 시간으로 나눈
  근사값입니다.

## 실제 서버 부하 시나리오

`rss_load_scenario_runner`는 Linux 전용 실행 파일입니다. 기본 내장 모드는
loopback 임시 포트에서 실제 서버를 시작하고 TCP 시나리오를 수행합니다.
`--host`와 `--port`를 지정하면 외부 서버에 접속합니다. 실행마다 결과를
버리는 warm-up 1회 후 `--repeat` 횟수만큼 측정합니다. 내장 모드는 매회 새 서버를
시작하며, 외부 모드는 같은 서버에 매회 새 연결로 접속합니다.

Linux 개발 빌드 후 다음 세 시나리오를 실행할 수 있습니다.

```bash
cmake --preset linux-dev
cmake --build --preset linux-dev --target rss_load_scenario_runner --parallel

./build/linux-dev/rss_load_scenario_runner --scenario broadcast --clients 100 --messages 100 --repeat 5 --workers 4
./build/linux-dev/rss_load_scenario_runner --scenario multi-room --clients 100 --rooms 10 --messages 100 --repeat 5 --workers 4
./build/linux-dev/rss_load_scenario_runner --scenario slow-client --clients 20 --slow-clients 1 --messages 2000 --payload-bytes 1291 --repeat 5 --workers 4
```

명령행 형식과 기본값은 다음과 같습니다. 모든 옵션은 값이 필요합니다.

```text
rss_load_scenario_runner \
  [--host IPv4 --port N] \
  [--scenario <broadcast|multi-room|slow-client>] \
  [--clients N] [--rooms N] [--messages N] [--payload-bytes N] \
  [--slow-clients N] [--repeat N] [--workers N] \
  [--rate-per-client N] [--max-in-flight N] [--timeout-seconds N]
```

| 옵션 | 기본값 | 의미 |
| --- | ---: | --- |
| `--host`, `--port` | 생략 | 함께 지정하면 해당 외부 IPv4 서버 사용; port는 1..65535 |
| `--scenario` | `broadcast` | 측정할 시나리오 |
| `--clients` | `10` | 연결할 전체 클라이언트 수 |
| `--rooms` | `2` | 다중 방 시나리오의 방 수 |
| `--messages` | `100` | 송신 클라이언트별 채팅 수 |
| `--payload-bytes` | `256` | 채팅 payload 크기(byte) |
| `--slow-clients` | `1` | 느린 클라이언트 수 |
| `--repeat` | `5` | warm-up 뒤 측정 반복 수 |
| `--workers` | `4` | 로컬 서버 worker 수 |
| `--rate-per-client` | `0` | 빠른 클라이언트별 초당 송신 상한; 0은 속도 제한 없음 |
| `--max-in-flight` | `0` | 방별 아직 모든 빠른 reader가 받지 않은 채팅 요청 상한; 0은 일반 시나리오 무제한, slow-client 자동 설정 |
| `--timeout-seconds` | `30` | setup 뒤 송신·수신 측정 구간의 제한 시간(초) |

`--rate-per-client`와 `--max-in-flight` 외 수치 옵션은 1 이상이어야 합니다.
`--payload-bytes`는 64 이상 1291 이하여야 하며, `multi-room`의 방 수는 클라이언트 수를 넘을 수 없습니다.
`slow-client`의 느린 클라이언트 수는 전체 클라이언트 수보다 작아야 합니다.
일반 `broadcast`와 `multi-room`은 서버의 세션별 pending write 기본 한도
1 MiB를 사용하고, `slow-client`는 분리 검증을 위해 32 KiB를 사용합니다.

`--clients`와 `--workers`는 각각 최대 1000, `--rate-per-client`는 최대
1,000,000, `--timeout-seconds`는 최대 3600입니다. 지연 표본과 중복 검출
키의 메모리를 제한하기 위해 한 번의 실행에서 기대하는 최대 수신 수는
5,000,000으로 제한합니다. 단일 방은 `빠른 client 수² × messages`, 다중
방은 `각 방 크기²의 합 × messages`로 계산하며 실행 전에 검사합니다.
느린 client는 송신·지연 표본에 포함하지 않습니다.

### 지속 부하

다음은 client별 10건/초 이하로 300건을 보내 약 30초 동안 측정하는 예입니다.
실제 성능 기록은 Debug 대신 sanitizer를 끈 Release 빌드를 사용합니다.

```bash
./build/release/rss_load_scenario_runner \
  --scenario broadcast --clients 20 --messages 300 --payload-bytes 256 \
  --rate-per-client 10 --max-in-flight 8 --timeout-seconds 60 \
  --repeat 3 --workers 2
```

첫 송신은 즉시 시작하고, 다음 송신은 직전 송신 완료 시점에서 간격을 둡니다.
실행이 지연되어도 밀린 요청을 한꺼번에 보내지 않습니다. 방별 window는
가장 늦은 빠른 reader를 기준으로 갱신하며 다중 방은 서로 독립적으로
진행합니다. 같은 방의 송신자에게 순번을 부여해 한 송신자가 window를
계속 차지하지 않게 합니다. 시간 제한이나 같은 방의 실패가 발생하면
window와 속도 대기를 깨워 종료합니다.

`slow-client`의 자동 window는 `max(1, pending write 한도 / 최대 패킷 크기)`로
계산하며 기본 설정은 8입니다. 명시한 window가 이 안전 상한을 넘으면
실행을 거절합니다. 느린 reader의 수신 버퍼가 충분히 쌓이지 않는 짧고 낮은
부하는 정상 client가 모두 성공해도 느린 client 종료 조건을 만족하지 못할
수 있습니다.

`--messages`는 여전히 유한한 종료 조건입니다. rate는 보장 처리량이 아니라
송신 상한이며, window 대기나 스케줄링 지연 때문에 실제 송신량은 더 낮을 수
있습니다. 지연 표본은 실제 송신 직전부터 수신까지로, 송신 전 rate/window
대기 시간은 제외합니다. 따라서 이 결과는 응답에 따라 부하를 조절하는 조건의
지연이며, 고정 도착률 과부하나 최대 처리량으로 해석하지 않습니다. 시간 제한은
연결·로그인·방 준비 및 서버 종료 시간을 포함한 전체 프로세스 제한이 아닙니다.

### 외부 서버와 분리 실행

`--host`와 `--port`를 함께 지정하면 실행기가 서버를 생성하거나 종료하지
않고 이미 실행 중인 서버로 접속합니다. IPv4 숫자 주소만 지원하며 DNS와
IPv6는 지원하지 않습니다. 단일 방과 다중 방에서 기존 rate/window/timeout을
사용할 수 있습니다. 서버 설정을 바꾸지 않으므로 외부 모드에서 명시적인
`--workers`는 인자 오류입니다.

운영 서버 빌드와 DB migration은 README의 절차를 사용합니다. 아래 예시는
CPU 0–3을 사용할 수 있는 Linux에서 서로 겹치지 않는 CPU를 배정합니다.
다른 머신에서 측정할 때는 서버 listen 주소와 실행기의 `--host`를 서로
접근 가능한 주소로 바꿉니다.

```bash
# 서버 터미널: RSS_DATABASE_URL은 실험용 DB 주소로 미리 설정
RSS_OBSERVABILITY_INTERVAL_SECONDS=10 \
  taskset -c 0,1 ./build/release/rss_server 127.0.0.1 19090 2 \
  > server.ndjson 2> server-errors.ndjson

# 부하 생성기 터미널
taskset -c 2,3 ./build/release/rss_load_scenario_runner \
  --host 127.0.0.1 --port 19090 --scenario broadcast \
  --clients 20 --messages 300 --payload-bytes 256 --repeat 3 \
  --rate-per-client 10 --max-in-flight 8 --timeout-seconds 60 \
  > client-results.txt
```

warm-up과 반복마다 고유한 사용자·방 이름을 사용합니다. 실행기가 자신의
TCP 연결을 닫으면 서버가 참가 상태를 정리하지만 PostgreSQL 사용자 레코드는
남습니다. 반복 측정용 서버와 DB를 사용하고 측정 종료 후 서버에는 SIGTERM을
보내 최종 통계까지 보관합니다. 외부 서버의 재시작·DB 초기화는 실행기가
대신 수행하지 않습니다.

외부 모드의 환경 줄은 **부하 생성기의** commit·CPU·compiler 정보이며
`environment_scope=load-generator workers=unknown`으로 구분합니다. 서버의
commit·빌드·CPU·worker 설정과 DB 조건은 따로 기록해야 합니다. 실행 결과는
`server_stats=unavailable`을 출력하고 서버 카운터는 전부 생략합니다. 누락된
카운터를 0으로 처리하면 안 됩니다. 서버의 NDJSON 통계는 별도로 수집하며,
누적 통계에는 warm-up·준비·모든 반복·연결 정리가 포함될 수 있어 단일 run의
통계와 같지 않습니다.

`slow-client` 성공 판정에는 실제 pending write 종료 수가 필요합니다.
외부 서버 통계를 자동 수집하지 않는 현재 외부 모드에서는 이 시나리오를
거절하고 내장 모드를 사용합니다. 별도 프로세스와 CPU affinity만으로 물리
머신·메모리·커널·네트워크의 자원 공유까지 제거되는 것은 아닙니다.

### 출력과 종료 코드

첫 줄은 비교 조건을 기록하는 `environment` 줄이고, 이어지는 각 `run` 줄은
warm-up을 제외한 한 번의 측정 결과입니다. 값은 공백으로 구분된 `key=value`
형식이며 run 번호는 1부터 시작합니다.

| `environment` 필드 | 의미 |
| --- | --- |
| `commit` | 빌드에 기록된 Git commit |
| `os`, `kernel`, `cpu` | 실행 환경의 운영체제, kernel, CPU 식별 정보 |
| `compiler`, `build_type` | 빌드에 기록된 compiler와 build type |
| `workers` | 내장 서버의 worker 수; 외부 모드는 `unknown` |
| `environment_scope` | 외부 모드에서 `load-generator`를 출력해 환경 정보의 대상을 구분 |
| `requested_slow_receive_buffer_bytes` | 느린 클라이언트에 요청하는 socket 수신 버퍼 크기; 운영체제가 실제 크기를 조정할 수 있음 |

각 `run` 줄의 주요 필드는 다음과 같습니다. 서버 카운터는 수집 가능한
경우에만 출력하며 `target`, `host`, `port`는 줄 끝에 붙습니다.

| 필드 | 의미 |
| --- | --- |
| `target`, `host`, `port` | `embedded` 또는 `external`과 요청 접속 대상; 내장 port 0은 자동 할당 요청 |
| `server_stats` | `available`이면 아래 서버 카운터를 출력, `unavailable`이면 전부 생략 |
| `run`, `scenario`, `clients`, `rooms` | 측정 반복 번호와 적용된 시나리오·클라이언트 수·effective 방 수; `broadcast`와 `slow-client`의 `rooms`는 `1` |
| `messages_per_sender`, `payload_bytes`, `slow_clients`, `repeats` | 해당 결과를 재현하는 요청 입력 |
| `rate_per_client`, `max_in_flight`, `effective_max_in_flight` | 요청한 송신 상한·방별 window와 실제 적용 window |
| `timeout_seconds`, `effective_timeout_ms` | 요청 제한 시간과 실제 측정 제한 시간(ms) |
| `sent` | 실제 전송에 성공한 채팅 요청 수 |
| `expected`, `received` | 방별 실제 성공 전송 수에 reader 수를 곱한 기대 broadcast 수와 실제 수신 수 |
| `missing`, `duplicates`, `unexpected` | 누락, 중복, 예상하지 않은 broadcast 수 |
| `failed_clients` | setup, 송신 또는 수신이 실패한 클라이언트 수; 첫 setup 실패 뒤 미시도 client도 포함 |
| `client_setup_*`, `client_send_*`, `client_receive_*` | 실제 관측한 단계별 실패 원인 수. 접미사는 `peer_closed`, `socket_error`, `timeout`, `protocol`, `other` |
| `elapsed_sec` | 마지막 barrier 참여자 도착부터 끝까지의 측정 경과 시간(초) |
| `throughput_broadcasts_per_sec` | `received / elapsed_sec`로 계산한 초당 수신 broadcast 수 |
| `p50_ms`, `p95_ms`, `p99_ms` | 수신한 broadcast 지연 시간의 백분위 값(ms) |
| `read_pauses`, `inbound_queue_full`, `outbound_budget_rejections` | 읽기 일시정지, 입력 queue 포화, 출력 예산 거절 횟수 |
| `handler_exceptions` | worker handler에서 빠져나와 해당 세션을 종료한 예외 횟수 |
| `slow_client_disconnects`, `rejected_connections` | 느린 클라이언트 종료와 연결 거절 횟수 |
| `disconnect_*` | I/O 스레드가 실제로 제거한 연결의 이유별 누적 수. 아래 분류 참고 |
| `worker_parked_limit_failures`, `worker_invalid_sequence_failures`, `worker_deferred_failures` | worker가 세션을 처음 실패 상태로 바꾼 이유별 누적 수 |
| `max_inbound_queue_size`, `max_outbound_queue_size`, `max_session_pending_write_bytes` | 측정 중 관찰한 입력 queue, 출력 queue, 세션별 미전송 byte의 최대값 |

종료 코드 `0`은 모든 측정 반복이 시나리오 성공 조건을 만족했음을 뜻합니다.
`1`은 측정은 끝났지만 누락·중복·예상 밖 수신·클라이언트 실패가 있거나,
`slow-client`에서 요청한 수만큼 느린 클라이언트가 종료되지 않았음을 뜻합니다.
client connect, login, 방 생성·참가 같은 setup 실패도 `run` 줄을 남기고
종료 코드 `1`을 반환합니다. 첫 실패 뒤 미시도 client는 `failed_clients`에
포함됩니다. `2`는 잘못된 인자이며 사용법을 함께 출력합니다. `3`은 서버
시작·설정 검증이나 실행기 내부 오류가 발생했음을 뜻합니다.

### 실패 원인 해석

클라이언트 원인은 상대의 정상 EOF인 `peer_closed`, 시스템 호출 오류인
`socket_error`, 제한 시간 초과인 `timeout`, 프로토콜/서버 오류 응답인
`protocol`, 그 밖의 예외인 `other`로 구분합니다. 예외 메시지나 채팅 본문은
원인 필드에 출력하지 않습니다. 미시도 클라이언트는 원인별 수에 포함하지
않습니다. 한 클라이언트가 송신과 수신 양쪽에서 실패할 수 있으므로 단계별
합계는 `failed_clients`와 다를 수 있습니다.

서버의 `disconnect_` 접미사는 `peer_closed`, `socket_error`, `protocol_error`,
`idle_timeout`, `worker_requested`, `pending_write_limit`, `close_after_flush`,
`shutdown`입니다. 각각 상대 EOF, 소켓 오류, 패킷 해석 오류, 유휴 시간 초과,
worker 종료 명령, 미전송 byte 상한, 오류 응답 송신 후 종료, 서버 정리 중
종료를 뜻합니다. 연결 하나의 실제 종료는 한 이유에만 기록합니다.

소켓 오류가 있으면 서버 표준 오류의 `socket_error` NDJSON을 함께 수집합니다.
`timestamp_unix_ms`, `session_id`, `fd`, `operation`, `error_code`,
`epoll_events`, `pending_write_bytes`로 발생 시점과 경로를 구분합니다.
`session_id`는 서버 수명 안에서만 고유하며 fd는 재사용될 수 있습니다.

- `epoll_error`: `EPOLLERR`에서 읽은 `SO_ERROR`. `error_code=0`도 그대로 기록
- `getsockopt_so_error`: `SO_ERROR` 조회 자체의 실패 errno
- `recv`, `send`: 실패한 시스템 호출의 errno
- `send_zero`: 양수 길이 송신의 0 반환. errno가 정의되지 않아 `error_code=0`

`epoll_events`는 epoll 오류 경로에서만 실제 이벤트 마스크이며 나머지는 0입니다.
`pending_write_bytes`는 사용자 공간 송신 queue 잔량이며 커널 미전송량이나
상대의 미수신량이 아닙니다. 진단 출력 실패 시에도 기존 연결 정리와 누적
카운터 갱신은 계속합니다. 로그는 오류 때 동기 출력하므로 오류가 몰리거나
표준 오류 수집이 느리면 I/O 스레드 타이밍에 영향을 줄 수 있습니다.
`RSS_OBSERVABILITY_INTERVAL_SECONDS=0`도 이 오류 로그를 끄지 않습니다.
오류 번호만으로 상대가 reset을 보낸 이유까지 확정할 수는 없습니다.

worker의 실패 판정과 I/O의 종료는 서로 다른 단계입니다. 예를 들어
`worker_parked_limit_failures`와 `disconnect_worker_requested`가 함께 증가하면
세션별 대기 이벤트 상한 거절 후 종료 명령이 처리된 것입니다. 두 수를
더해서 종료 연결 수로 해석하지 않습니다. `slow_client_disconnects`는 기존
호환 필드로 `disconnect_pending_write_limit`과 같은 종료를 가리킵니다.
snapshot은 실행 중 누적 관측값이며, 부하 결과는 측정 종료 시점에 수집해
이후 서버 정리로 인한 종료를 포함하지 않습니다.

### 비교 제약

이 실행기는 절대적인 최대 성능을 인증하는 도구가 아니라 회귀 비교를 위한
도구입니다. 서로 다른 commit을 비교할 때는 Linux 배포판과 kernel, CPU,
compiler, build type, `--workers`, 모든 시나리오 인자를 같게 유지하고,
동일한 종류의 실행 환경(물리 Linux, WSL2, 가상 머신, 컨테이너)을 사용해야
합니다. `environment` 줄을 결과와 함께 보관하고, 실패 관련 필드가 0이 아닌
반복은 정상 처리량으로 해석하지 않습니다.

한 실행기에서 외부 IPv4 서버로 접속할 수 있지만, 여러 부하 생성기의 분산
조정이나 원격 서버 내부 통계 조회, TLS, WAN 지연·패킷 손실 주입은 지원하지
않습니다.
