#ifndef OPENMW_PROCESSORPLAYERINVENTORY_HPP
#define OPENMW_PROCESSORPLAYERINVENTORY_HPP

#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerInventory : public PlayerProcessor
    {
    public:
        ProcessorPlayerInventory()
        {
            BPP_INIT(ID_PLAYER_INVENTORY)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            if (!player.acceptInventoryPacket())
            {
                if (player.hasAcceptedInventoryPacket)
                {
                    // Correct stale inventory mutations back to the last
                    // authoritative packet accepted by the server.
                    packet.setPlayer(&player);
                    packet.Send(player.guid);
                }
                return;
            }

            ServerEvents::playerEvent("OnPlayerInventory", player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERINVENTORY_HPP
