#ifndef OPENMW_PROCESSORPLAYERATTACK_HPP
#define OPENMW_PROCESSORPLAYERATTACK_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/ServerSimulation.hpp"
#include "PlayerMovementSnapshot.hpp"

namespace mwmp
{
    class ProcessorPlayerAttack : public PlayerProcessor
    {
        PlayerPacketController *playerController;
    public:
        ProcessorPlayerAttack()
        {
            BPP_INIT(ID_PLAYER_ATTACK)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            if (player.creatureStats.mDead)
                return;

            if (!acceptSequencedPlayerCombatEvent(player))
                return;

            Networking::getPtr()->getServerSimulation().applyPlayerAttack(player);

            if (player.attack.isHit && player.attack.target.isPlayer)
                player.sendToLoadedAndGuid(&packet, player.attack.target.guid);
            else
                player.sendToLoaded(&packet);
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERATTACK_HPP
