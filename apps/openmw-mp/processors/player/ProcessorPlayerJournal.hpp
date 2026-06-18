#ifndef OPENMW_PROCESSORPLAYERJOURNAL_HPP
#define OPENMW_PROCESSORPLAYERJOURNAL_HPP

#include "../../PlayerPacketDecisionEvent.hpp"
#include "../../PlayerQuestStateStore.hpp"
#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerJournal : public PlayerProcessor
    {
    public:
        ProcessorPlayerJournal()
        {
            BPP_INIT(ID_PLAYER_JOURNAL)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            sendPlayerPacketDecisionEvent(player,
                PlayerPacketDecisionEvent{
                    .packetName = "journal",
                    .reason = "accepted",
                    .accepted = true,
                    .corrected = false,
                    .attemptedItemCount = player.journalChanges.size(),
                    .authoritativeItemCount = player.journalChanges.size(),
                    .loadSnapshot = player.journalChangesAreLoad,
                });

            PlayerQuestStateStore::get().applyJournalChanges(player);

            ServerEvents::playerEvent("OnPlayerJournal", player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERJOURNAL_HPP
