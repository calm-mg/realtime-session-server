# Architecture

## Runtime Model

```text
Main Thread
  -> accept4 / epoll_wait
  -> non-blocking recv / send
  -> Session lifecycle
  -> enqueue decoded packets

Worker Threads
  -> pop SessionEvent
  -> MessageRouter
  -> RoomService / Lobby / Room
  -> enqueue OutboundMessage

Main Thread
  -> drain outbound queue
  -> enable EPOLLOUT
  -> flush pending writes
```

The server keeps socket ownership in the main I/O thread. Worker threads never call `send`, `recv`, or `epoll_ctl`; they only process commands and produce encoded outbound packets.

## Layers

- `net`: Linux-specific socket, epoll, session, and worker pool code.
- `protocol`: binary packet framing and stream decoding.
- `domain`: `User`, `Room`, `Lobby`, and position state.
- `service`: login, room membership, chat routing, position routing, disconnect cleanup.

## Concurrency

Inbound packets flow through `BlockingQueue<SessionEvent>`.
Outbound packets flow back through `BlockingQueue<OutboundMessage>`.

The current implementation uses ordinary mutexes around shared room/user state. That keeps the code easy to reason about while still showing the server shape expected in production systems. Later optimizations can shard rooms, pin rooms to workers, or replace coarse locks with actor-style room ownership.

## Session Lifecycle

1. `accept4` creates a non-blocking socket.
2. A `Session` receives a generated `session_id`.
3. `recv` bytes are fed into `PacketCodec`.
4. Complete packets are pushed to workers.
5. Worker output is queued back to the I/O thread.
6. The I/O thread enables `EPOLLOUT` and flushes pending writes.
7. Disconnect or timeout triggers cleanup through `RoomService::disconnect`.
