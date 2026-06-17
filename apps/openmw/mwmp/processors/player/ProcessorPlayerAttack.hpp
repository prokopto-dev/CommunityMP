#ifndef OPENMW_PROCESSORPLAYERATTACK_HPP
#define OPENMW_PROCESSORPLAYERATTACK_HPP

#include "apps/openmw/mwmp/Main.hpp"
#include "apps/openmw/mwbase/world.hpp"
#include "../PlayerProcessor.hpp"
#include "apps/openmw/mwmp/MechanicsHelper.hpp"

namespace mwmp
{
    class ProcessorPlayerAttack final: public PlayerProcessor
    {
    public:
        ProcessorPlayerAttack()
        {
            BPP_INIT(ID_PLAYER_ATTACK)
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
                MechanicsHelper::processAttack(player->attack, dedicatedPlayer.getPtr(), false);
            }
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERATTACK_HPP

