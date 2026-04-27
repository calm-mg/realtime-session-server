# Protocol

All packets use a 4-byte big-endian header.

```cpp
struct PacketHeader {
    uint16_t size; // total packet bytes, including this header
    uint16_t type; // PacketType
};
```

Maximum packet size is `4096` bytes.

## Packet Types

| Type | Name | Payload |
| ---: | --- | --- |
| 1 | `LOGIN_REQ` | UTF-8 display name |
| 2 | `LOGIN_RES` | `OK|user_id=...|session_id=...|name=...` |
| 10 | `CREATE_ROOM_REQ` | UTF-8 room name |
| 11 | `CREATE_ROOM_RES` | `OK|event=CREATE_ROOM|room_id=...|...` |
| 12 | `JOIN_ROOM_REQ` | decimal room id |
| 13 | `JOIN_ROOM_RES` | `OK|event=JOIN_ROOM|room_id=...|...` |
| 14 | `LEAVE_ROOM_REQ` | empty |
| 15 | `LEAVE_ROOM_RES` | `OK|event=LEAVE_ROOM|room_id=...|...` |
| 20 | `CHAT_REQ` | UTF-8 chat message |
| 21 | `POSITION_UPDATE` | `float x`, `float y`, each encoded as network-order IEEE-754 bits |
| 22 | `ROOM_BROADCAST` | event text payload |
| 30 | `PING` | empty |
| 31 | `PONG` | `PONG` |
| 100 | `ERROR` | error message |

The header is binary even when some MVP payloads are text. That keeps stream framing, partial reads, malformed packet detection, and packet routing realistic without overcomplicating the initial domain protocol.

## Stream Behavior

TCP may split or coalesce packets. `PacketCodec` keeps an internal byte buffer and only emits complete packets. Invalid sizes or oversized packets raise `ProtocolError`.
