# 프로토콜

모든 packet은 4-byte big-endian header를 사용합니다.

```cpp
struct PacketHeader {
    uint16_t size; // header를 포함한 전체 packet byte 수
    uint16_t type; // PacketType
};
```

최대 packet size는 `4096` bytes입니다.

## Packet Type

| Type | 이름 | Payload |
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
| 21 | `POSITION_UPDATE` | `float x`, `float y`, network-order IEEE-754 bit |
| 22 | `ROOM_BROADCAST` | event text payload |
| 30 | `PING` | empty |
| 31 | `PONG` | `PONG` |
| 100 | `ERROR` | error message |

header는 binary이고, 일부 MVP payload는 text format을 사용합니다. 이렇게 두면 초기 domain protocol을 과하게 복잡하게 만들지 않으면서도 TCP stream framing, partial read, packet coalescing, malformed packet detection을 코드에서 직접 다룰 수 있습니다.

## TCP Stream 처리

TCP는 packet 경계를 보존하지 않습니다. 하나의 packet이 여러 `recv`로 쪼개질 수 있고, 여러 packet이 한 번의 `recv`로 합쳐질 수도 있습니다.

`PacketCodec`은 내부 byte buffer를 유지하고, 완성된 packet만 반환합니다. size가 header보다 작거나 최대 크기를 초과하면 `ProtocolError`를 발생시킵니다.
