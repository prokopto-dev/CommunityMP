#ifndef OPENMW_PROCESSORACTORSTATSDYNAMIC_HPP
#define OPENMW_PROCESSORACTORSTATSDYNAMIC_HPP

#include "../ActorProcessor.hpp"
#include "ActorSequenceCoalescing.hpp"
#include "apps/openmw-mp/ServerEventDispatcher.hpp"

namespace mwmp
{
    class ProcessorActorStatsDynamic : public ActorProcessor
    {
    public:
        ProcessorActorStatsDynamic()
        {
            BPP_INIT(ID_ACTOR_STATS_DYNAMIC)
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            // Send only to players who have the cell loaded
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);
            if (rejectClientActorPacketForServerOwnedCell(serverCell, actorList, "ID_ACTOR_STATS_DYNAMIC"))
                return;

            if (serverCell != nullptr && serverCell->hasAuthority(actorList.guid))
            {
                if (!filterActorStatsDynamicToServerAccepted(serverCell, actorList))
                    return;

                serverCell->readActorList(packetID, &actorList);
                ServerEvents::actorStatsDynamic(player.getId(), actorList.cell.getDescription().c_str());
                serverCell->sendToLoaded(&packet, &actorList);

                // Echo the normalized server-accepted stats back to the authority
                // sender so local NPC health/death state cannot drift from the cell.
                packet.setActorList(&actorList);
                packet.Send(actorList.guid);
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORSTATSDYNAMIC_HPP
