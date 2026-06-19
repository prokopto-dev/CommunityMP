#ifndef OPENMW_PROCESSORPLAYERSPELLSACTIVE_HPP
#define OPENMW_PROCESSORPLAYERSPELLSACTIVE_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/ServerNetworking.hpp"
#include "apps/openmw-mp/ServerSimulation.hpp"

namespace mwmp
{
    class ProcessorPlayerSpellsActive : public PlayerProcessor
    {
    public:
        ProcessorPlayerSpellsActive()
        {
            BPP_INIT(ID_PLAYER_SPELLS_ACTIVE)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            ServerNetworking* networking = ServerNetworking::getPtr();
            if (networking != nullptr && networking->getServerSimulation().runtime().canOwnActorAuthority())
                return;

            ServerEvents::playerEvent("OnPlayerSpellsActive", player.getId());
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERSPELLSACTIVE_HPP
