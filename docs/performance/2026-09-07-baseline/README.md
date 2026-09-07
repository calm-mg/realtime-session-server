# Linux 컨테이너 예비 성능 기준값

2026-09-07에 고정된 환경에서 측정한 첫 기준값입니다. 정상 burst 30회와
느린 클라이언트 5회는 누락·중복·예상 밖 메시지·정상 클라이언트 실패 없이
완료했습니다. 큰 burst는 연결 실패와 누락이 발생했습니다. 짧은 실행과 큰
편차 때문에 이 결과를 최대 처리량이나 CI 성능 합격선으로 사용하지 않습니다.

## 대상과 환경

- 소스: `3d97432ae1cf691992a938b0db175fa791d51d18`, 빌드 시 작업 트리 clean
- Apple M1 Pro 8코어, 메모리 16 GiB, macOS 14.6 위의 Linux 가상 환경
- Docker 24.0.2, VM 4 CPU, 컨테이너 CPU quota 4개·메모리 4 GiB·swap 0
- Ubuntu 24.04.4 arm64, Linux 5.15.49-linuxkit-pr, GCC 13.3.0,
  CMake 3.28.3, Release(`-O3 -DNDEBUG`), Google Benchmark 1.9.5
- 서버와 부하 클라이언트가 같은 프로세스에서 loopback TCP와 CPU를 공유
- 메모리 사용자 저장소 사용. DB, Qt, TLS, 외부 네트워크 및 로그인 지연 제외
- 다른 빌드·테스트를 중단한 후 순차 실행. CPU pinning, 주파수 고정,
  호스트의 완전한 격리는 적용하지 않음

정확한 환경과 실행 파일 SHA-256은 [environment.json](environment.json)에
기록했습니다. Google Benchmark의 가상 환경 CPU 주파수 출력은 실제 CPU
클럭으로 해석하지 않습니다. 결과 JSON에서 컨테이너 hostname만 제거했습니다.

## 측정 조건과 집계

각 실행은 warm-up 1회를 제외하고 5회 측정하며 매번 서버를 새로 시작합니다.
아래 표의 처리량과 p95/p99는 **각 실행 값의 중앙값**입니다. p95/p99를
전체 표본의 백분위로 합산한 값이 아닙니다. 처리량 단위는 수신 broadcast/s이며
괄호는 5회 최솟값–최댓값입니다. 입력 요청/s와 구분해야 합니다.

- 정상: 20명, 각 20개 메시지, payload 256바이트, 송신 간격 없는 burst.
  단일 방은 회당 8,000건, 4개 방은 회당 2,000건을 수신합니다.
- 느린 클라이언트: 3명 중 읽지 않는 1명, 정상 2명이 각 2,000개 송신,
  payload 1,291바이트, 정상 수신 기대값 8,000건. 공유 outstanding window로
  전송을 제한하므로 정상 burst와 처리량을 직접 비교하지 않습니다.
- worker 순서는 2 → 1 → 4이며 각 worker에서 단일 방 → 다중 방 순서입니다.
  이어서 느린 클라이언트와 마이크로벤치마크를 실행했습니다. 순서 무작위화나
  서로 다른 날짜의 재측정은 하지 않았습니다.
- inbound/outbound queue 각 4,096개, 읽기 high/low watermark 3,072/2,048,
  세션 parked event 32개, 정상 pending write 1 MiB를 사용했습니다.
  느린 클라이언트 측정의 pending write는 32 KiB입니다. 느린 클라이언트의
  수신 버퍼 설정 요청값은 1,024바이트입니다.

## 반복 결과

| 시나리오 | worker | 처리량 중앙값 (범위) | p95 ms 중앙값 (범위) | p99 ms 중앙값 (범위) |
| --- | ---: | ---: | ---: | ---: |
| 단일 방 | 1 | 130,331 (44,410–146,196) | 17.983 (15.816–118.484) | 57.478 (18.262–137.893) |
| 단일 방 | 2 | 333,292 (127,604–412,456) | 18.326 (17.712–56.384) | 18.868 (18.141–56.928) |
| 단일 방 | 4 | 125,078 (118,352–135,524) | 20.562 (19.377–24.561) | 25.275 (20.002–60.448) |
| 다중 방 | 1 | 43,935 (29,900–45,409) | 43.463 (42.675–58.944) | 44.291 (43.239–62.319) |
| 다중 방 | 2 | 44,624 (43,219–44,952) | 43.576 (42.550–45.075) | 43.795 (43.499–45.318) |
| 다중 방 | 4 | 43,393 (41,939–44,705) | 43.195 (42.621–44.807) | 43.934 (42.938–46.463) |
| 느린 클라이언트 | 2 | 15,058 (11,548–27,600) | 0.549 (0.388–0.607) | 5.899 (0.523–41.459) |

35회 모두 실패·누락·중복·예상 밖 메시지 0입니다. 느린 클라이언트는 매회
정확히 1명 종료됐고, pending write 최댓값은 32,499바이트로 상한 이내였습니다.
정상 단일 방은 모든 worker 조건에서 outbound queue 최댓값 4,096에 도달했습니다.
이는 queue 사용량 관측이며, 그 자체로 메시지 손실을 의미하지 않습니다.

단일 방 worker 2의 중앙값이 가장 높지만 처리량 범위가 넓고 한 실행이
19–63 ms에 불과합니다. 다중 방은 worker를 늘려도 이 실험에서 처리량 증가가
관측되지 않았습니다. 이 숫자만으로 `RoomService` mutex를 병목으로 확정하거나
worker 기본값을 변경하지 않습니다. 약 40 ms의 지연 구간도 관측되지만,
TCP 동작과 스케줄링을 별도로 계측하기 전에는 원인을 단정하지 않습니다.

## 실패한 예비 측정

정상 조건 선정 전에 worker 2, 20명에서 각 1,000개와 100개 burst를 실행했습니다.
각 조건은 warm-up을 제외한 1회 관측이며, 실패한 결과도 삭제하지 않았습니다.

| 시나리오 | 1인당 메시지 | 기대 수신 | 실제 수신 | 누락 | 실패 클라이언트 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 단일 방 | 1,000 | 400,000 | 2,181 | 397,819 | 20 |
| 다중 방 | 1,000 | 100,000 | 1,256 | 98,744 | 20 |
| 단일 방 | 100 | 40,000 | 849 | 39,151 | 20 |
| 다중 방 | 100 | 10,000 | 336 | 9,664 | 20 |

모두 종료 코드 1입니다. 기존 과부하 카운터는 0이어서 이 출력만으로 연결
종료 원인을 구분할 수 없습니다. 코드상 세션 parked event 상한 초과도 종료
경로가 되므로 확인 대상이지만, 이번 측정에서는 직접 계측하지 않았습니다.
실패한 실행의 처리량·백분위는 수신에 성공한 일부 메시지만 반영하므로 정상
처리량으로 인용하지 않습니다. 세부 값은 `pilot-*.json`에 있습니다.

## 마이크로벤치마크

각 항목은 최소 측정 0.2초, warm-up 0.1초, 5회 반복입니다. 아래는
ChatFanout 호출 1회당 CPU 시간 중앙값입니다. 네트워크와 outbound queue는
포함하지 않으며, 단일 스레드이므로 mutex 경합 측정도 아닙니다.

| 방 인원 | CPU 시간 |
| ---: | ---: |
| 1 | 0.833 µs |
| 10 | 1.087 µs |
| 100 | 3.626 µs |
| 1,000 | 28.826 µs |

전체 codec·통계·채팅 결과는 [microbenchmarks-data.json](microbenchmarks-data.json)에
있습니다. 협상을 수행하지 않던 이전 ChatFanout은 거절 경로를 측정했으므로
이전 수치를 성능 개선 비교에 사용하지 않습니다.

## 재현

해당 커밋의 저장소 루트에서 같은 사양의 Linux 컨테이너를 준비합니다.
호스트 저장소를 `/src:ro`로 마운트하고 컨테이너 내부에 빌드합니다.

```bash
docker run -d --name rss-baseline --cpus=4 --memory=4g --memory-swap=4g \
  --mount "type=bind,source=$PWD,target=/src,readonly" \
  ubuntu:24.04 sleep infinity
docker exec -it rss-baseline bash
```

컨테이너 내부에서 실행합니다. 패키지 저장소의 향후 변경으로 도구 버전이
달라질 수 있으므로 결과 비교 전 `environment.json`과 버전을 대조합니다.
원 측정은 동일한 고정 버전의 의존 소스 캐시를 사용했습니다.

```bash
apt-get update
apt-get install -y cmake g++ ninja-build libpq-dev ca-certificates git
git config --global --add safe.directory /src
cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DRSS_BUILD_NETWORK_TARGETS=ON -DRSS_BUILD_POSTGRES=ON \
  -DRSS_BUILD_BENCHMARKS=ON
cmake --build /build -j 4

for workers in 2 1 4; do
  for scenario in broadcast multi-room; do
    /build/rss_load_scenario_runner --scenario "$scenario" --clients 20 \
      --rooms 4 --messages 20 --payload-bytes 256 --workers "$workers" --repeat 5
  done
done
/build/rss_load_scenario_runner --scenario slow-client --clients 3 \
  --slow-clients 1 --messages 2000 --payload-bytes 1291 --workers 2 --repeat 5
/build/rss_microbenchmarks --benchmark_min_time=0.2s \
  --benchmark_min_warmup_time=0.1 --benchmark_repetitions=5 \
  --benchmark_out=/tmp/microbenchmarks.json --benchmark_out_format=json
```

각 JSON은 실제 명령, 시작·종료 시각, 종료 코드, stdout/stderr를 보관합니다.
실행기가 출력하지 않는 warm-up 결과는 포함하지 않습니다. 각 `*-w*.json`의
`stdout`에서 `run=` 행을 key=value로 읽고 `throughput_broadcasts_per_sec`,
`p95_ms`, `p99_ms`의 중앙값을 계산하면 표를 재현할 수 있습니다.

## 후속 측정

연결 종료 이유를 관측하고 송신 rate/window를 제어하는 지속 부하 조건을
먼저 확보합니다. 이후 별도 프로세스 또는 호스트에서 부하를 생성하고,
여러 측정 세션의 결과와 CPU profile·mutex 대기 시간을 확인합니다.
현재는 공유 mutex를 유지하며 성능 합격선은 정하지 않습니다. 작업 순서는
[프로젝트 상태](../../project-status.md)에서 관리합니다.
