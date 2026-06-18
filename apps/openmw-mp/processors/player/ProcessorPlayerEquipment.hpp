#ifndef OPENMW_PROCESSORPLAYEREQUIPMENT_HPP
#define OPENMW_PROCESSORPLAYEREQUIPMENT_HPP

#include "../../PlayerPacketDecisionEvent.hpp"
#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerEquipment : public PlayerProcessor
    {
    public:
        ProcessorPlayerEquipment()
        {
            BPP_INIT(ID_PLAYER_EQUIPMENT)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            const std::uint32_t attemptedSequence = player.equipmentSequence;
            const std::size_t attemptedChangedSlotCount = player.equipmentIndexChanges.size();
            const bool attemptedExchangeFullInfo = player.exchangeFullInfo;
            const bool attemptedEquipmentWasValid = player.hasValidEquipmentItems();
            const bool attemptedSequenceWasStale = player.hasAcceptedEquipmentPacket
                && !isNewerPlayerEquipmentSequence(player.equipmentSequence, player.acceptedEquipmentSequence)
                && !(player.exchangeFullInfo && player.equipmentSequence == player.acceptedEquipmentSequence);

            if (!player.acceptEquipmentPacket())
            {
                sendPlayerPacketDecisionEvent(player,
                    PlayerPacketDecisionEvent{
                        .packetName = "equipment",
                        .reason = attemptedEquipmentWasValid && attemptedSequenceWasStale
                            ? "stale_sequence"
                            : "invalid_equipment",
                        .accepted = false,
                        .corrected = player.hasAcceptedEquipmentPacket,
                        .attemptedSequence = attemptedSequence,
                        .authoritativeSequence = player.equipmentSequence,
                        .acceptedSequence = player.acceptedEquipmentSequence,
                        .attemptedChangedSlotCount = attemptedChangedSlotCount,
                        .authoritativeChangedSlotCount = player.equipmentIndexChanges.size(),
                        .exchangeFullInfo = attemptedExchangeFullInfo,
                    });

                if (player.hasAcceptedEquipmentPacket)
                {
                    // Correct invalid or stale equipment snapshots back to
                    // the last authoritative server-accepted equipment state.
                    const bool previousExchangeFullInfo = player.exchangeFullInfo;
                    player.exchangeFullInfo = true;
                    packet.setPlayer(&player);
                    packet.Send(player.guid);
                    player.exchangeFullInfo = previousExchangeFullInfo;
                }
                return;
            }

            sendPlayerPacketDecisionEvent(player,
                PlayerPacketDecisionEvent{
                    .packetName = "equipment",
                    .reason = "accepted",
                    .accepted = true,
                    .corrected = false,
                    .attemptedSequence = attemptedSequence,
                    .authoritativeSequence = player.equipmentSequence,
                    .acceptedSequence = player.acceptedEquipmentSequence,
                    .attemptedChangedSlotCount = attemptedChangedSlotCount,
                    .authoritativeChangedSlotCount = player.equipmentIndexChanges.size(),
                    .exchangeFullInfo = attemptedExchangeFullInfo,
                });

            player.sendToLoaded(&packet);

            ServerEvents::playerEvent("OnPlayerEquipment", player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYEREQUIPMENT_HPP
