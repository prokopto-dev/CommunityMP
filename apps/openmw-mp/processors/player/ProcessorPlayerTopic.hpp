#ifndef OPENMW_PROCESSORPLAYERTOPIC_HPP
#define OPENMW_PROCESSORPLAYERTOPIC_HPP

#include "../../PlayerPacketDecisionEvent.hpp"
#include "../../PlayerQuestStateStore.hpp"
#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerTopic : public PlayerProcessor
    {
    public:
        ProcessorPlayerTopic()
        {
            BPP_INIT(ID_PLAYER_TOPIC)
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            sendPlayerPacketDecisionEvent(player,
                PlayerPacketDecisionEvent{
                    .packetName = "topic",
                    .reason = "accepted",
                    .accepted = true,
                    .corrected = false,
                    .attemptedItemCount = player.topicChanges.size(),
                    .authoritativeItemCount = player.topicChanges.size(),
                    .loadSnapshot = player.topicChangesAreLoad,
                });

            PlayerQuestStateStore::get().applyTopicChanges(player);

            ServerEvents::playerEvent("OnPlayerTopic", player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERTOPIC_HPP
