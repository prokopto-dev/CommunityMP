#ifndef OPENMW_PROCESSORPLAYERBOOK_HPP
#define OPENMW_PROCESSORPLAYERBOOK_HPP

#include "../../PlayerPacketDecisionEvent.hpp"
#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerBook : public PlayerProcessor
    {
    public:
        ProcessorPlayerBook()
        {
            BPP_INIT(ID_PLAYER_BOOK)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            sendPlayerPacketDecisionEvent(player,
                PlayerPacketDecisionEvent{
                    .packetName = "book",
                    .reason = "accepted",
                    .accepted = true,
                    .corrected = false,
                    .attemptedItemCount = player.bookChanges.size(),
                    .authoritativeItemCount = player.bookChanges.size(),
                    .loadSnapshot = player.bookChangesAreLoad,
                });

            ServerEvents::playerEvent("OnPlayerBook", player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERBOOK_HPP
