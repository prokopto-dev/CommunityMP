#ifndef OPENMW_PROCESSORPLAYERLUAEVENT_HPP
#define OPENMW_PROCESSORPLAYERLUAEVENT_HPP

#include "../../CommunityMpLuaEventQueue.hpp"
#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerLuaEvent final : public PlayerProcessor
    {
    public:
        ProcessorPlayerLuaEvent()
        {
            BPP_INIT(ID_PLAYER_LUA_EVENT)
        }

        void Do(PlayerPacket& packet, BasePlayer* player) override
        {
            static_cast<void>(packet);

            if (isRequest() || player == nullptr)
                return;

            if (!player->hasValidClientLuaEvent())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Rejected invalid server Lua event for client");
                return;
            }

            CommunityMpLuaEventQueue::pushServerEvent(*player, isLocal());
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE,
                "Queued server Lua event for OpenMW Lua: schema=%u sequence=%u namespace=%s event=%s bytes=%zu",
                static_cast<unsigned int>(player->luaEvent.schemaVersion),
                static_cast<unsigned int>(player->luaEvent.sequence),
                player->luaEvent.namespaceName.c_str(),
                player->luaEvent.eventName.c_str(),
                player->luaEvent.payload.size());
        }
    };
}

#endif // OPENMW_PROCESSORPLAYERLUAEVENT_HPP
