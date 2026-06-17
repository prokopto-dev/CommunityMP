#ifndef OPENMW_PROCESSORACTORANIMPLAY_HPP
#define OPENMW_PROCESSORACTORANIMPLAY_HPP

#include "../ActorProcessor.hpp"
#include "ActorSequenceCoalescing.hpp"

namespace mwmp
{
    class ProcessorActorAnimPlay : public ActorProcessor
    {
    public:
        ProcessorActorAnimPlay()
        {
            BPP_INIT(ID_ACTOR_ANIM_PLAY)
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            // Send only to players who have the cell loaded
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell != nullptr && serverCell->hasAuthority(actorList.guid))
            {
                if (!filterActorCombatToServerAccepted(serverCell, actorList, false))
                    return;

                serverCell->sendToLoaded(&packet, &actorList);
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORANIMPLAY_HPP
