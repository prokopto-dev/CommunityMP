#ifndef OPENMW_PROCESSORACTORTEST_HPP
#define OPENMW_PROCESSORACTORTEST_HPP

#include "../ActorProcessor.hpp"
#include "ActorSequenceCoalescing.hpp"

namespace mwmp
{
    class ProcessorActorTest : public ActorProcessor
    {
    public:
        ProcessorActorTest()
        {
            BPP_INIT(ID_ACTOR_TEST)
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            // Send only to players who have the cell loaded
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell != nullptr && serverCell->hasAuthority(actorList.guid))
            {
                if (!filterActorListToKnownLiveActors(serverCell, actorList, false))
                    return;

                serverCell->sendToLoaded(&packet, &actorList);
            }

            if (serverCell != nullptr && serverCell->hasAuthority(actorList.guid))
                Script::Call<Script::CallbackIdentity("OnActorTest")>(player.getId(), actorList.cell.getDescription().c_str());
        }
    };
}

#endif //OPENMW_PROCESSORACTORTEST_HPP
