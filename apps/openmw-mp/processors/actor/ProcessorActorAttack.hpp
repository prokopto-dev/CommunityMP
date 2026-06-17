#ifndef OPENMW_PROCESSORACTORATTACK_HPP
#define OPENMW_PROCESSORACTORATTACK_HPP

#include <vector>

#include "../ActorProcessor.hpp"
#include "apps/openmw-mp/ServerNetworking.hpp"
#include "apps/openmw-mp/ServerSimulation.hpp"

namespace mwmp
{
    class ProcessorActorAttack : public ActorProcessor
    {
    public:
        ProcessorActorAttack()
        {
            BPP_INIT(ID_ACTOR_ATTACK)
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            // Send only to players who have the cell loaded
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell != nullptr && serverCell->hasPlayer(&player))
            {
                if (!ServerNetworking::getPtr()->getServerSimulation().acceptActorAttacks(actorList, *serverCell))
                    return;

                std::vector<PacketGuid> targetGuids;
                for (const BaseActor& actor : actorList.baseActors)
                {
                    if (actor.attack.isHit && actor.attack.target.isPlayer)
                        targetGuids.push_back(actor.attack.target.guid);
                }

                serverCell->sendToLoadedAndGuids(&packet, &actorList, targetGuids);
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORATTACK_HPP
