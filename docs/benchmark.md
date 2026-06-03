# 벤치마크

현재 포함된 load test client는 간단한 smoke test 용도입니다. 동시 접속과 요청 처리량을 빠르게 확인할 수 있지만, 최종 성능 수치로 공개하기에는 latency, broadcast fanout, queue saturation, slow client 영향 같은 지표가 부족합니다.

```bash
./build/rss_server 0.0.0.0 7777 4
./build/rss_load_test_client 127.0.0.1 7777 1000 100
```

## 기본 기록 항목

벤치마크 결과를 기록할 때는 최소한 다음 항목을 함께 남깁니다.

- CPU 모델과 core 수
- OS와 kernel version
- compiler version과 build type
- git commit 또는 server version
- worker/shard count
- client count
- room count
- messages per client
- payload size
- total messages sent
- elapsed seconds
- messages per second
- p50/p95/p99 latency
- queue full count
- overload 또는 disconnect count

## 측정 시나리오

포트폴리오에서 의미 있는 성능 근거를 만들기 위해 다음 시나리오를 추가할 계획입니다.

- `ping`: 다수 클라이언트의 request/response round-trip latency 측정
- `single-room-broadcast`: 한 room에 많은 client가 있을 때 broadcast fanout 측정
- `many-room-broadcast`: 작은 room 여러 개가 동시에 동작할 때 처리량과 지연 측정
- `slow-client`: 일부 client가 읽지 않을 때 pending write와 backpressure 동작 확인
- `fragmented-packets`: 하나의 packet이 여러 번의 write로 쪼개지는 상황 확인
- `coalesced-packets`: 여러 packet이 한 번의 write로 합쳐지는 상황 확인
- `large-payload-rejection`: 최대 packet size 초과 시 rejection/disconnect 확인
- `idle-timeout-cleanup`: idle session 정리와 room cleanup 확인

## 도구 방향

서버 전체를 대상으로 하는 scenario benchmark는 별도 load test client로 측정합니다. `PacketCodec`, bounded queue, shard routing처럼 작은 단위의 성능 비교는 Google Benchmark를 도입해 microbenchmark로 분리합니다.

## 해석 기준

단순히 messages/sec만 높게 찍는 것보다, 서버가 부하를 받는 상황에서 어떤 병목이 생기고 어떻게 개선했는지가 중요합니다.

이 프로젝트에서는 다음 비교를 목표로 합니다.

- baseline: I/O thread + worker pool + unbounded queue + mutex 기반 `RoomService`
- improved: `eventfd` wakeup + bounded queue + backpressure + room shard ownership

최종 문서에는 baseline과 improved 결과를 같은 환경에서 측정해 p50/p95/p99 latency, broadcast 처리량, overload count를 함께 기록합니다.
