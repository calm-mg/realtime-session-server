#include "TestSupport.h"

#include "rss/service/RoomService.h"

int main()
{
    rss::service::RoomService service;

    const auto alice = service.login(100, "alice");
    const auto bob = service.login(200, "bob");
    RSS_EXPECT(alice.id != bob.id);

    const auto create = service.createRoom(100, "arena");
    RSS_EXPECT(create.ok);
    RSS_EXPECT(create.room_id == 1);
    RSS_EXPECT(create.recipients.size() == 1);

    const auto join = service.joinRoom(200, create.room_id);
    RSS_EXPECT(join.ok);
    RSS_EXPECT(join.recipients.size() == 2);

    const auto chat = service.chat(100);
    RSS_EXPECT(chat.ok);
    RSS_EXPECT(chat.recipients.size() == 2);

    const auto position = service.updatePosition(200, rss::domain::Position{7.0F, 9.0F});
    RSS_EXPECT(position.ok);
    RSS_EXPECT(position.recipients.size() == 2);

    const auto leave = service.leaveRoom(100);
    RSS_EXPECT(leave.ok);
    RSS_EXPECT(leave.recipients.size() == 2);

    const auto disconnect = service.disconnect(200);
    RSS_EXPECT(disconnect.ok);

    testPassed("RoomServiceTest");
    return 0;
}
