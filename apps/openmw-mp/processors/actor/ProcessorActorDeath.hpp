#ifndef OPENMW_PROCESSORACTORDEATH_HPP
#define OPENMW_PROCESSORACTORDEATH_HPP

#include "../ActorProcessor.hpp"
#include "ActorSequenceCoalescing.hpp"

namespace mwmp
{
    class ProcessorActorDeath : public ActorProcessor
    {
    public:
        ProcessorActorDeath()
        {
            BPP_INIT(ID_ACTOR_DEATH)
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            // Send only to players who have the cell loaded
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);
            if (rejectClientActorPacketForServerOwnedCell(serverCell, actorList, "ID_ACTOR_DEATH"))
                return;

            if (serverCell != nullptr && serverCell->hasAuthority(actorList.guid))
            {
                if (!filterActorDeathToServerAccepted(serverCell, actorList))
                    return;

                serverCell->readActorList(packetID, &actorList);
                ServerEvents::actorEvent("OnActorDeath", player.getId(), actorList.cell.getDescription().c_str());

                serverCell->sendToLoaded(&packet, &actorList);
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORDEATH_HPP
