#ifndef OPENMW_PROCESSORPLAYERCAST_HPP
#define OPENMW_PROCESSORPLAYERCAST_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/ServerNetworking.hpp"
#include "apps/openmw-mp/ServerSimulation.hpp"
#include "PlayerMovementSnapshot.hpp"

namespace mwmp
{
    class ProcessorPlayerCast : public PlayerProcessor
    {
        PlayerPacketController *playerController;
    public:
        ProcessorPlayerCast()
        {
            BPP_INIT(ID_PLAYER_CAST)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            if (player.creatureStats.mDead)
                return;

            if (!acceptSequencedPlayerCombatEvent(player))
                return;

            if (!ServerNetworking::getPtr()->getServerSimulation().acceptPlayerCast(player))
                return;

            if (player.cast.target.isPlayer)
                player.sendToLoadedAndGuid(&packet, player.cast.target.guid);
            else
                player.sendToLoaded(&packet);
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERCAST_HPP
