#ifndef OPENMW_PROCESSORPLAYERSTATS_DYNAMIC_HPP
#define OPENMW_PROCESSORPLAYERSTATS_DYNAMIC_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/ServerEventDispatcher.hpp"
#include "apps/openmw-mp/ServerNetworking.hpp"

namespace mwmp
{
    class ProcessorPlayerStatsDynamic : public PlayerProcessor
    {
    public:
        ProcessorPlayerStatsDynamic()
        {
            BPP_INIT(ID_PLAYER_STATS_DYNAMIC)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            if (!ServerNetworking::getPtr()->getServerSimulation().acceptPlayerStatsDynamic(player))
            {
                if (player.hasAcceptedStatsDynamicPacket)
                {
                    // Send the restored authoritative snapshot back to the sender
                    // so rejected health/death updates do not leave local drift.
                    packet.setPlayer(&player);
                    packet.Send(player.guid);
                }
                return;
            }

            player.sendToLoaded(&packet);

            ServerEvents::playerStatsDynamic(player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERSTATS_DYNAMIC_HPP
