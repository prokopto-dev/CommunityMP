#ifndef OPENMW_PROCESSORPLAYERPOSITION_HPP
#define OPENMW_PROCESSORPLAYERPOSITION_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/ServerNetworking.hpp"
#include "apps/openmw-mp/ServerSimulation.hpp"

namespace mwmp
{
    class ProcessorPlayerPosition : public PlayerProcessor
    {
    public:
        ProcessorPlayerPosition()
        {
            BPP_INIT(ID_PLAYER_POSITION)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            ServerNetworking::getPtr()->getServerSimulation().acceptPlayerMovementSnapshot(player, packet);
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERPOSITION_HPP
