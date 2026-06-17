#ifndef OPENMW_PROCESSORACTORCAST_HPP
#define OPENMW_PROCESSORACTORCAST_HPP

#include <vector>

#include "../ActorProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"
#include "apps/openmw-mp/ServerSimulation.hpp"

namespace mwmp
{
    class ProcessorActorCast : public ActorProcessor
    {
    public:
        ProcessorActorCast()
        {
            BPP_INIT(ID_ACTOR_CAST)
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            // Send only to players who have the cell loaded
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell != nullptr && serverCell->hasPlayer(&player))
            {
                if (!Networking::getPtr()->getServerSimulation().acceptActorCasts(actorList, *serverCell))
                    return;

                std::vector<PacketGuid> targetGuids;
                for (const BaseActor& actor : actorList.baseActors)
                {
                    if (actor.cast.target.isPlayer)
                        targetGuids.push_back(actor.cast.target.guid);
                }

                serverCell->sendToLoadedAndGuids(&packet, &actorList, targetGuids);
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORCAST_HPP
