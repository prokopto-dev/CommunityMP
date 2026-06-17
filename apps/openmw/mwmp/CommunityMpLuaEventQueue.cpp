#include "CommunityMpLuaEventQueue.hpp"

#include <deque>
#include <mutex>

namespace
{
    constexpr std::size_t maxQueuedServerLuaEvents = 128;

    std::mutex sServerLuaEventMutex;
    std::deque<mwmp::CommunityMpLuaEvent> sServerLuaEvents;
}

namespace mwmp
{
    void CommunityMpLuaEventQueue::pushServerEvent(const BasePlayer& player, bool subjectIsLocal)
    {
        if (!player.hasValidClientLuaEvent())
            return;

        CommunityMpLuaEvent event;
        event.schemaVersion = player.luaEvent.schemaVersion;
        event.sequence = player.luaEvent.sequence;
        event.namespaceName = player.luaEvent.namespaceName;
        event.eventName = player.luaEvent.eventName;
        event.payload = player.luaEvent.payload;
        event.subjectGuid = player.guid;
        event.subjectIsLocal = subjectIsLocal;

        std::lock_guard lock(sServerLuaEventMutex);
        while (sServerLuaEvents.size() >= maxQueuedServerLuaEvents)
            sServerLuaEvents.pop_front();
        sServerLuaEvents.push_back(std::move(event));
    }

    std::vector<CommunityMpLuaEvent> CommunityMpLuaEventQueue::takeServerEvents()
    {
        std::lock_guard lock(sServerLuaEventMutex);
        std::vector<CommunityMpLuaEvent> events;
        events.reserve(sServerLuaEvents.size());
        while (!sServerLuaEvents.empty())
        {
            events.push_back(std::move(sServerLuaEvents.front()));
            sServerLuaEvents.pop_front();
        }
        return events;
    }

    void CommunityMpLuaEventQueue::clearServerEvents()
    {
        std::lock_guard lock(sServerLuaEventMutex);
        sServerLuaEvents.clear();
    }
}
