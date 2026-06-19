#ifndef OPENMW_PROCESSORPLAYERDEATH_HPP
#define OPENMW_PROCESSORPLAYERDEATH_HPP


#include "apps/openmw/mwbase/world.hpp"
#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerDeath final: public PlayerProcessor
    {
    public:
        ProcessorPlayerDeath()
        {
            BPP_INIT(ID_PLAYER_DEATH)
        }

        virtual void Do(PlayerPacket &packet, BasePlayer *player)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_DEATH from server");

            if (isLocal())
            {
                LOG_APPEND(TimedLog::LOG_INFO, "- Packet was about me");

                if (!player->acceptCombatPacket())
                    return;

                player->acceptPositionPacket();
                static_cast<LocalPlayer*>(player)->die();
            }
            else if (player != 0)
            {
                LOG_APPEND(TimedLog::LOG_INFO, "- Packet was about %s", player->npc.mName.c_str());

                DedicatedPlayer& dedicatedPlayer = static_cast<DedicatedPlayer&>(*player);
                if (!dedicatedPlayer.isCombatPacketSequenceAllowed())
                    return;

                if (!dedicatedPlayer.normalizePositionPacket())
                    return;

                dedicatedPlayer.acceptCurrentCombatPacket();
                dedicatedPlayer.die();
                player->acceptCurrentStatsDynamicPacket();
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERDEATH_HPP

