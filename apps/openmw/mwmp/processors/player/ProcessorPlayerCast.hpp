#ifndef OPENMW_PROCESSORPLAYERCAST_HPP
#define OPENMW_PROCESSORPLAYERCAST_HPP

#include "apps/openmw/mwmp/Main.hpp"
#include "apps/openmw/mwbase/world.hpp"
#include "../PlayerProcessor.hpp"
#include "apps/openmw/mwmp/MechanicsHelper.hpp"

namespace mwmp
{
    class ProcessorPlayerCast final: public PlayerProcessor
    {
    public:
        ProcessorPlayerCast()
        {
            BPP_INIT(ID_PLAYER_CAST)
        }

        virtual void Do(PlayerPacket &packet, BasePlayer *player)
        {
            if (!isLocal() && player != 0)
            {
                DedicatedPlayer& dedicatedPlayer = static_cast<DedicatedPlayer&>(*player);
                if (!dedicatedPlayer.isCombatPacketSequenceAllowed())
                    return;

                if (!dedicatedPlayer.normalizePositionPacket())
                    return;

                dedicatedPlayer.acceptCurrentCombatPacket();
                MechanicsHelper::processCast(player->cast, dedicatedPlayer.getPtr(), false);
            }
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERCAST_HPP

