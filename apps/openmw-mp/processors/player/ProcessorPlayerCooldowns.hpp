#ifndef OPENMW_PROCESSORPLAYERCOOLDOWNS_HPP
#define OPENMW_PROCESSORPLAYERCOOLDOWNS_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/ServerNetworking.hpp"
#include "apps/openmw-mp/ServerSimulation.hpp"

namespace mwmp
{
    class ProcessorPlayerCooldowns : public PlayerProcessor
    {
    public:
        ProcessorPlayerCooldowns()
        {
            BPP_INIT(ID_PLAYER_COOLDOWNS)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            ServerNetworking* networking = ServerNetworking::getPtr();
            if (networking != nullptr && networking->getServerSimulation().runtime().canOwnActorAuthority())
                return;

            ServerEvents::playerEvent("OnPlayerCooldowns", player.getId());
        }
    };
}


#endif //OPENMW_PROCESSORPLAYERCOOLDOWNS_HPP
