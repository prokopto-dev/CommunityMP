#ifndef OPENMW_PROCESSORACTORPOSITION_HPP
#define OPENMW_PROCESSORACTORPOSITION_HPP

#include "../ActorProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"
#include "apps/openmw-mp/ServerSimulation.hpp"

namespace mwmp
{
    class ProcessorActorPosition : public ActorProcessor
    {
    public:
        ProcessorActorPosition()
        {
            BPP_INIT(ID_ACTOR_POSITION)
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            // Send only to players who have the cell loaded
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell != nullptr && serverCell->hasPlayer(&player))
                Networking::getPtr()->getServerSimulation().acceptActorMovementSnapshot(packet, actorList, *serverCell);
        }
    };
}

#endif //OPENMW_PROCESSORACTORPOSITION_HPP
