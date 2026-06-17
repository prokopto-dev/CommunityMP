#ifndef OPENMW_PROCESSORPLAYERLUAEVENT_HPP
#define OPENMW_PROCESSORPLAYERLUAEVENT_HPP

#include "../../CommunityMpClientLuaEventHandler.hpp"
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

        void Do(PlayerPacket& packet, Player& player) override
        {
            if (!player.hasValidClientLuaEvent())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "Rejected invalid client Lua event from %s", player.npc.mName.c_str());
                return;
            }

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE,
                "Accepted client Lua event from %s: schema=%u sequence=%u namespace=%s event=%s bytes=%zu",
                player.npc.mName.c_str(),
                static_cast<unsigned int>(player.luaEvent.schemaVersion),
                static_cast<unsigned int>(player.luaEvent.sequence),
                player.luaEvent.namespaceName.c_str(),
                player.luaEvent.eventName.c_str(),
                player.luaEvent.payload.size());

            static_cast<void>(CommunityMpClientLuaEventHandler::handlePlayerEvent(player));
        }
    };
}

#endif // OPENMW_PROCESSORPLAYERLUAEVENT_HPP
