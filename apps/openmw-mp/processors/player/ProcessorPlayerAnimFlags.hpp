#ifndef OPENMW_PROCESSORPLAYERANIMFLAGS_HPP
#define OPENMW_PROCESSORPLAYERANIMFLAGS_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/ServerSimulation.hpp"
#include "PlayerMovementSnapshot.hpp"

namespace mwmp
{
    class ProcessorPlayerAnimFlags : public PlayerProcessor
    {
    public:
        ProcessorPlayerAnimFlags()
        {
            BPP_INIT(ID_PLAYER_ANIM_FLAGS)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            if (player.hasStaleAnimFlagsPacket())
            {
                player.restoreAcceptedAnimFlagsPacket();
                return;
            }

            if (!player.acceptAnimFlagsPacket())
                return;

            if (!normalizePlayerMovementSnapshot(player))
                return;

            ServerNetworking::getPtr()->getServerSimulation().sendPlayerVisualStateToLoaded(player, packet);
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERANIMFLAGS_HPP
