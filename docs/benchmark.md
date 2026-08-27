# 벤치마크 실행과 해석

성능 검사는 목적이 다른 두 도구로 나뉩니다.

- `rss_microbenchmarks`: 네트워크 없이 작은 코드 경로의 실행 시간을 측정
- `rss_load_test_client`: 실제 TCP 연결과 `PING`/`PONG` 왕복 시간을 측정
- `rss_load_scenario_runner`: 로컬 실제 서버의 방 broadcast, 다중 방, 느린
  클라이언트 격리와 내부 과부하 통계를 반복 측정

마이크로벤치마크 숫자는 서버의 동시 접속 처리량이 아닙니다. 반대로
TCP 부하 테스트 결과만으로 어느 함수가 느린지는 알 수 없습니다.

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

`rss_load_scenario_runner`는 Linux 전용 실행 파일입니다. loopback 임시
포트에서 실제 서버를 시작하고 실제 TCP 연결로 시나리오를 수행하므로,
실행 중인 원격 서버는 필요하지 않습니다. 실행마다 결과를 버리는 warm-up을
1회 수행한 뒤 `--repeat` 횟수만큼 새 서버에서 측정합니다.

Linux 개발 빌드 후 다음 세 시나리오를 실행할 수 있습니다.

```bash
cmake --preset linux-dev
cmake --build --preset linux-dev --target rss_load_scenario_runner --parallel

./build/linux-dev/rss_load_scenario_runner --scenario broadcast --clients 100 --messages 100 --repeat 5 --workers 4
./build/linux-dev/rss_load_scenario_runner --scenario multi-room --clients 100 --rooms 10 --messages 100 --repeat 5 --workers 4
./build/linux-dev/rss_load_scenario_runner --scenario slow-client --clients 20 --slow-clients 1 --messages 2000 --payload-bytes 3939 --repeat 5 --workers 4
```

명령행 형식과 기본값은 다음과 같습니다. 모든 옵션은 값이 필요합니다.

```text
rss_load_scenario_runner \
  [--scenario <broadcast|multi-room|slow-client>] \
  [--clients N] [--rooms N] [--messages N] [--payload-bytes N] \
  [--slow-clients N] [--repeat N] [--workers N]
```

| 옵션 | 기본값 | 의미 |
| --- | ---: | --- |
| `--scenario` | `broadcast` | 측정할 시나리오 |
| `--clients` | `10` | 연결할 전체 클라이언트 수 |
| `--rooms` | `2` | 다중 방 시나리오의 방 수 |
| `--messages` | `100` | 송신 클라이언트별 채팅 수 |
| `--payload-bytes` | `256` | 채팅 payload 크기(byte) |
| `--slow-clients` | `1` | 느린 클라이언트 수 |
| `--repeat` | `5` | warm-up 뒤 측정 반복 수 |
| `--workers` | `4` | 로컬 서버 worker 수 |

수치 옵션은 1 이상이어야 합니다. `--payload-bytes`는 64 이상 3939 이하여야
하며, `multi-room`의 방 수는 클라이언트 수를 넘을 수 없습니다.
`slow-client`의 느린 클라이언트 수는 전체 클라이언트 수보다 작아야 합니다.
일반 `broadcast`와 `multi-room`은 서버의 세션별 pending write 기본 한도
1 MiB를 사용하고, `slow-client`는 분리 검증을 위해 32 KiB를 사용합니다.

### 출력과 종료 코드

첫 줄은 비교 조건을 기록하는 `environment` 줄이고, 이어지는 각 `run` 줄은
warm-up을 제외한 한 번의 측정 결과입니다. 값은 공백으로 구분된 `key=value`
형식이며 run 번호는 1부터 시작합니다.

| `environment` 필드 | 의미 |
| --- | --- |
| `commit` | 빌드에 기록된 Git commit |
| `os`, `kernel`, `cpu` | 실행 환경의 운영체제, kernel, CPU 식별 정보 |
| `compiler`, `build_type` | 빌드에 기록된 compiler와 build type |
| `workers` | `--workers`로 로컬 서버에 적용한 worker 수 |
| `requested_slow_receive_buffer_bytes` | 느린 클라이언트에 요청하는 socket 수신 버퍼 크기; 운영체제가 실제 크기를 조정할 수 있음 |

각 `run` 줄에는 다음 필드가 고정 순서로 출력됩니다.

| 필드 | 의미 |
| --- | --- |
| `run`, `scenario`, `clients`, `rooms` | 측정 반복 번호와 적용된 시나리오·클라이언트 수·effective 방 수; `broadcast`와 `slow-client`의 `rooms`는 `1` |
| `messages_per_sender`, `payload_bytes`, `slow_clients`, `repeats` | 해당 결과를 재현하는 요청 입력 |
| `sent` | 실제 전송에 성공한 채팅 요청 수 |
| `expected`, `received` | 방별 실제 성공 전송 수에 reader 수를 곱한 기대 broadcast 수와 실제 수신 수 |
| `missing`, `duplicates`, `unexpected` | 누락, 중복, 예상하지 않은 broadcast 수 |
| `failed_clients` | setup, 송신 또는 수신이 실패한 클라이언트 수; 첫 setup 실패 뒤 미시도 client도 포함 |
| `elapsed_sec` | 마지막 barrier 참여자 도착부터 끝까지의 측정 경과 시간(초) |
| `throughput_broadcasts_per_sec` | `received / elapsed_sec`로 계산한 초당 수신 broadcast 수 |
| `p50_ms`, `p95_ms`, `p99_ms` | 수신한 broadcast 지연 시간의 백분위 값(ms) |
| `read_pauses`, `inbound_queue_full`, `outbound_budget_rejections` | 읽기 일시정지, 입력 queue 포화, 출력 예산 거절 횟수 |
| `handler_exceptions` | worker handler에서 빠져나와 해당 세션을 종료한 예외 횟수 |
| `slow_client_disconnects`, `rejected_connections` | 느린 클라이언트 종료와 연결 거절 횟수 |
| `max_inbound_queue_size`, `max_outbound_queue_size`, `max_session_pending_write_bytes` | 측정 중 관찰한 입력 queue, 출력 queue, 세션별 미전송 byte의 최대값 |

종료 코드 `0`은 모든 측정 반복이 시나리오 성공 조건을 만족했음을 뜻합니다.
`1`은 측정은 끝났지만 누락·중복·예상 밖 수신·클라이언트 실패가 있거나,
`slow-client`에서 요청한 수만큼 느린 클라이언트가 종료되지 않았음을 뜻합니다.
client connect, login, 방 생성·참가 같은 setup 실패도 `run` 줄을 남기고
종료 코드 `1`을 반환합니다. 첫 실패 뒤 미시도 client는 `failed_clients`에
포함됩니다. `2`는 잘못된 인자이며 사용법을 함께 출력합니다. `3`은 서버
시작·설정 검증이나 실행기 내부 오류가 발생했음을 뜻합니다.

### 비교 제약

이 실행기는 절대적인 최대 성능을 인증하는 도구가 아니라 회귀 비교를 위한
도구입니다. 서로 다른 commit을 비교할 때는 Linux 배포판과 kernel, CPU,
compiler, build type, `--workers`, 모든 시나리오 인자를 같게 유지하고,
동일한 종류의 실행 환경(물리 Linux, WSL2, 가상 머신, 컨테이너)을 사용해야
합니다. `environment` 줄을 결과와 함께 보관하고, 실패 관련 필드가 0이 아닌
반복은 정상 처리량으로 해석하지 않습니다.

이 도구는 여러 머신의 분산 부하, 원격 서버의 내부 통계 조회, TLS, WAN 지연
또는 패킷 손실을 측정하지 않습니다.
