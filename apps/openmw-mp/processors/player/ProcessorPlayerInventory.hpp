#ifndef OPENMW_PROCESSORPLAYERINVENTORY_HPP
#define OPENMW_PROCESSORPLAYERINVENTORY_HPP

#include "../../PlayerPacketDecisionEvent.hpp"
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

            const std::uint32_t attemptedSequence = player.inventorySequence;
            const int attemptedAction = player.inventoryChanges.action;
            const std::size_t attemptedItemCount = player.inventoryChanges.items.size();

            if (!player.acceptInventoryPacket())
            {
                sendPlayerPacketDecisionEvent(player,
                    PlayerPacketDecisionEvent{
                        .packetName = "inventory",
                        .reason = "stale_sequence",
                        .accepted = false,
                        .corrected = player.hasAcceptedInventoryPacket,
                        .attemptedSequence = attemptedSequence,
                        .authoritativeSequence = player.inventorySequence,
                        .acceptedSequence = player.acceptedInventorySequence,
                        .attemptedAction = attemptedAction,
                        .authoritativeAction = player.inventoryChanges.action,
                        .attemptedItemCount = attemptedItemCount,
                        .authoritativeItemCount = player.inventoryChanges.items.size(),
                    });

                if (player.hasAcceptedInventoryPacket)
                {
                    // Correct stale inventory mutations back to the last
                    // authoritative packet accepted by the server.
                    packet.setPlayer(&player);
                    packet.Send(player.guid);
                }
                return;
            }

            sendPlayerPacketDecisionEvent(player,
                PlayerPacketDecisionEvent{
                    .packetName = "inventory",
                    .reason = "accepted",
                    .accepted = true,
                    .corrected = false,
                    .attemptedSequence = attemptedSequence,
                    .authoritativeSequence = player.inventorySequence,
                    .acceptedSequence = player.acceptedInventorySequence,
                    .attemptedAction = attemptedAction,
                    .authoritativeAction = player.inventoryChanges.action,
                    .attemptedItemCount = attemptedItemCount,
                    .authoritativeItemCount = player.inventoryChanges.items.size(),
                });

            ServerEvents::playerEvent("OnPlayerInventory", player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERINVENTORY_HPP
