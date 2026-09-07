# 송신 속도와 응답 대기 상한을 둔 지속 부하

큰 burst에서 확인한 세션 대기 이벤트 상한 초과와 별개로, 부하의 도착
속도를 제한했을 때 정상 처리가 지속되는지 확인합니다. 서버의 대기 이벤트
상한이나 worker 기본 동작은 바꾸지 않았습니다.

## 환경과 재현

측정 소스는 `770115e01b98c69854876e5d653839f00e75542e`이며 깨끗한 작업
트리에서 Release로 빌드했습니다. 원시 명령, 출력, 시작 시간, 종료 코드와
실행 파일 SHA-256은 [results.json](results.json)에 보관합니다.

- 호스트: Apple M1 Pro, 8 CPU core, 16 GiB RAM, macOS 14.6
- 실행 환경: Ubuntu 24.04 arm64 컨테이너, Linux 5.15.49-linuxkit-pr
- 자원 제한: CPU quota 4개, 메모리 4 GiB, swap 없음
- 빌드: GCC 13.3.0, CMake Release, sanitizer 비활성
- 서버와 부하 생성기: 같은 프로세스·컨테이너의 loopback TCP, worker 2개
- 반복: 조건마다 새 서버에서 warm-up 1회 후 측정 3회

```bash
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DRSS_BUILD_NETWORK_TARGETS=ON -DRSS_BUILD_POSTGRES=ON
cmake --build build/release --target rss_load_scenario_runner --parallel

./build/release/rss_load_scenario_runner \
  --scenario broadcast --clients 20 --rooms 4 --messages 300 \
  --payload-bytes 256 --slow-clients 1 --workers 2 --repeat 3 \
  --rate-per-client 10 --max-in-flight 8 --timeout-seconds 60
```

다중 방은 `--scenario multi-room`, 느린 client는 `--scenario slow-client
--payload-bytes 1291`로 바꾸고 나머지 인자는 그대로 유지합니다. 단일 방과
느린 client 시나리오에서 `--rooms`는 무시되며 실제 방 수는 1입니다.
느린 client 1명은 송신·지연 측정에서 제외하므로 정상 송신자는 19명입니다.

속도는 client별 10건/초 상한이며 첫 송신은 즉시 시작합니다. 방별 window
8건이 가장 늦은 정상 수신자의 진행을 기다릴 수 있으므로 실제 송신률은
설정한 상한보다 낮을 수 있습니다. 한 번에 client별 300건을 보내 약 30초
동안 측정하고, 제한 시간 60초에는 준비 단계와 서버 정리 시간을 포함하지
않습니다.

## 관측 결과

세 조건의 측정 9회 모두 종료 코드 0입니다. 매회 실제 전송 수는 요청한
메시지 수와 일치했고, 기대 수신과 실제 수신도 일치했습니다.

| 조건 | 회당 송신 / 수신 | 경과 시간(초) | 수신 broadcast/초 | p95(ms) | p99(ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 단일 방·20명·256byte | 6,000 / 120,000 | 30.450–30.484 | 3,936–3,941 | 28.113–29.366 | 40.515–40.769 |
| 4개 방·각 5명·256byte | 6,000 / 30,000 | 30.247–30.321 | 989–992 | 31.044–33.449 | 41.891–42.074 |
| 느린 1명·정상 19명·1,291byte | 5,700 / 108,300 | 30.651–31.582 | 3,429–3,533 | 40.805–41.194 | 42.573–51.518 |

표의 범위는 각 반복에서 계산한 값의 최솟값–최댓값이며 표본을 합친
백분위가 아닙니다. 수신 처리량은 broadcast 전달 건수이므로 방 크기와
정상 수신자 수가 다른 행을 서버 처리 능력 순위로 비교하지 않습니다.

9회 모두 누락·중복·예상 밖 수신·정상 client 실패가 0이고, 입력 queue
포화·출력 예산 거절·handler 예외·worker 세션 실패도 0입니다. 느린 client
조건에서는 매회 정확히 1개 연결만 pending write 상한으로 종료됐고,
정상 19명은 모든 메시지를 받았습니다. 나머지 조건에서는 연결 종료가
없었습니다.

이전의 무제한 burst 실패와 달리 송신 상한과 window를 적용한 조건은
지속해서 정상 처리됐습니다. 이는 해당 부하 조건이 성공한다는 관측이며,
서버 상한을 높였거나 큰 burst 수용 능력이 개선됐다는 뜻은 아닙니다.
p99는 정상 단일 방·다중 방에서도 약 41–42ms였으며, 느린 client 조건의
한 반복에서는 51.518ms였습니다. 지연 원인은 아직 분리 측정하지 않았습니다.

## 해석의 한계

이 결과는 낮은 고정 송신 상한과 응답에 따라 송신을 늦추는 window 조건의
회귀 자료입니다. 서버 최대 처리량, 동시 접속 한계 또는 긴 시간의 메모리
안정성을 입증하지 않습니다. 지연은 실제 송신 직전부터 수신까지이며
송신 전 pacing/window 대기 시간을 포함하지 않습니다.

서버와 부하 생성기가 CPU를 공유하므로 부하 생성기의 병목과 서버 병목을
구분할 수 없습니다. 후속 작업은 부하 생성 환경을 분리하고 여러 측정
세션에서 조건을 반복한 뒤, `RoomService` mutex 경합과 주요 실행 경로를
프로파일링하는 것입니다. 실제 병목이 확인된 뒤 구조 변경 여부를 결정합니다.
