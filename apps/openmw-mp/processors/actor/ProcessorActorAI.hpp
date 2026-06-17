#ifndef OPENMW_PROCESSORACTORAI_HPP
#define OPENMW_PROCESSORACTORAI_HPP

#include "../ActorProcessor.hpp"
#include "apps/openmw-mp/ServerNetworking.hpp"
#include "apps/openmw-mp/ServerSimulation.hpp"
#include "ActorSequenceCoalescing.hpp"

namespace mwmp
{
    class ProcessorActorAI : public ActorProcessor
    {
    public:
        ProcessorActorAI()
        {
            BPP_INIT(ID_ACTOR_AI)
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell != nullptr && serverCell->hasPlayer(&player))
            {
                if (!ServerNetworking::getPtr()->getServerSimulation().acceptActorAiSnapshot(actorList, *serverCell))
                    return;

                Script::Call<Script::CallbackIdentity("OnActorAI")>(player.getId(), actorList.cell.getDescription().c_str());
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORAI_HPP
