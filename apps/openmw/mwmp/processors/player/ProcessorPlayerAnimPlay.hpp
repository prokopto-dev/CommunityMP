#ifndef OPENMW_PROCESSORPLAYERANIMPLAY_HPP
#define OPENMW_PROCESSORPLAYERANIMPLAY_HPP

#include "apps/openmw/mwbase/world.hpp"
#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerAnimPlay final: public PlayerProcessor
    {
    public:
        ProcessorPlayerAnimPlay()
        {
            BPP_INIT(ID_PLAYER_ANIM_PLAY)
        }

        virtual void Do(PlayerPacket &packet, BasePlayer *player)
        {
            if (isLocal())
            {
                if (!player->acceptCombatPacket())
                    return;

                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_ANIM_PLAY about LocalPlayer from server");
                static_cast<LocalPlayer*>(player)->playAnimation();
            }
            else if (player != 0)
            {
                DedicatedPlayer& dedicatedPlayer = static_cast<DedicatedPlayer&>(*player);
                if (!dedicatedPlayer.isCombatPacketSequenceAllowed())
                    return;

                if (!dedicatedPlayer.normalizePositionPacket())
                    return;

                dedicatedPlayer.acceptCurrentCombatPacket();
                dedicatedPlayer.playAnimation();
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERANIMPLAY_HPP

