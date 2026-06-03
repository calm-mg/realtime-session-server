# 아키텍처

## 현재 런타임 모델

```text
I/O Thread
  -> accept4 / epoll_wait
  -> non-blocking recv / send
  -> Session 생명주기 관리
  -> decode된 packet을 queue에 push

Worker Threads
  -> pop SessionEvent
  -> MessageRouter
  -> RoomService / Lobby / Room
  -> enqueue OutboundMessage
  -> eventfd wakeup

I/O Thread
  -> eventfd readable event
  -> drain outbound queue
  -> enable EPOLLOUT
  -> flush pending writes
```

현재 서버는 socket ownership을 I/O thread에만 둡니다. worker thread는 `send`, `recv`, `epoll_ctl`을 호출하지 않고, command를 처리한 뒤 outbound packet만 생성합니다.

이 구조는 실시간 서버의 기본 경계를 보여주기 좋습니다. transport 계층과 domain/service 계층이 섞이지 않아 protocol, room, message routing 테스트를 socket 없이 실행할 수 있습니다.

## 계층

- `net`: Linux socket, `epoll`, session, worker pool
- `protocol`: binary packet framing, stream decoding
- `domain`: `User`, `Room`, `Lobby`, position state
- `service`: login, room membership, chat routing, position routing, disconnect cleanup

## 동시성 모델

Inbound packet은 `BlockingQueue<SessionEvent>`를 통해 worker thread로 전달됩니다.
Outbound packet은 `BlockingQueue<OutboundMessage>`를 통해 다시 I/O thread로 돌아옵니다.

현재 `RoomService`는 공유 room/user state를 mutex로 보호합니다. baseline 구현으로는 이해하기 쉽지만, 성능 포트폴리오 관점에서는 병목 후보가 명확합니다. 이후 개선에서는 room state를 shard별 worker가 소유하는 actor-style 구조로 바꾸고, 전역 mutex 의존도를 줄이는 방향을 목표로 합니다.

worker thread가 outbound queue에 completion을 넣으면 `eventfd`로 I/O thread를 깨웁니다. I/O thread는 해당 fd의 readable event를 받으면 counter를 drain하고 outbound queue를 처리합니다. socket과 `epoll_ctl` ownership은 계속 I/O thread에만 있습니다.

## 세션 생명주기

1. `accept4`로 non-blocking socket을 생성한다.
2. `Session`에 `session_id`를 부여한다.
3. `recv`로 읽은 bytes를 `PacketCodec`에 넣는다.
4. 완성된 packet을 worker queue로 전달한다.
5. worker 결과를 outbound queue에 넣고 `eventfd`로 I/O thread를 깨운다.
6. I/O thread가 eventfd counter와 outbound queue를 drain한다.
7. I/O thread가 `EPOLLOUT`을 켜고 pending write를 flush한다.
8. disconnect 또는 timeout이 발생하면 `RoomService::disconnect`로 room 상태를 정리한다.

## 향후 개선 방향

현재 구조에서 성능 개선 대상으로 보는 지점은 다음과 같습니다.

- unbounded queue를 bounded queue로 바꾸고 overload 동작 명시
- slow client가 write buffer를 무한히 키우지 못하도록 backpressure 적용
- room/user state를 하나의 mutex로 보호하는 구조를 room shard ownership으로 변경
- benchmark에서 throughput뿐 아니라 p50/p95/p99 latency와 broadcast fanout 지연 측정

개선 후에도 핵심 ownership 규칙은 유지합니다. socket과 `epoll_ctl`은 I/O thread가 소유하고, worker 또는 shard thread는 domain command 처리와 outbound completion 생성만 담당합니다.

목표 구조는 다음과 같습니다.

```text
I/O Thread
  -> epoll_wait
  -> read / decode / command enqueue
  -> eventfd wakeup 수신
  -> completion drain
  -> session write queue flush

Room Shards
  -> bounded command queue pop
  -> room membership / chat / position 처리
  -> completion queue push
  -> eventfd write
```

이 방향은 Linux `epoll` 서버를 직접 구현했다는 장점을 유지하면서, 실제 서버에서 중요한 latency, overload, slow client, shared-state 병목까지 설명할 수 있게 만드는 것을 목표로 합니다.
