#ifndef OPENMW_PROCESSORACTORCELLCHANGE_HPP
#define OPENMW_PROCESSORACTORCELLCHANGE_HPP

#include "../ActorProcessor.hpp"
#include "ActorSequenceCoalescing.hpp"
#include "apps/openmw-mp/ServerEventDispatcher.hpp"

#include <cstdlib>

namespace mwmp
{
    inline bool isAdjacentExteriorCellChange(const ESM::Cell& sourceCell, const ESM::Cell& destinationCell)
    {
        return sourceCell.isExterior() && destinationCell.isExterior()
            && std::abs(sourceCell.mData.mX - destinationCell.mData.mX) <= 1
            && std::abs(sourceCell.mData.mY - destinationCell.mData.mY) <= 1;
    }

    class ProcessorActorCellChange : public ActorProcessor
    {
    public:
        ProcessorActorCellChange()
        {
            BPP_INIT(ID_ACTOR_CELL_CHANGE)
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            std::vector<BaseActor> acceptedActors;
            acceptedActors.reserve(actorList.baseActors.size());
            for (const BaseActor& actor : actorList.baseActors)
            {
                if (isFiniteActorMovementSnapshot(actor)
                    && actor.cell.getDescription() != actorList.cell.getDescription())
                    acceptedActors.push_back(actor);
            }

            actorList.baseActors = acceptedActors;
            actorList.count = static_cast<unsigned int>(actorList.baseActors.size());
            if (actorList.count == 0)
                return;

            bool isAccepted = false;
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell != nullptr)
            {
                std::vector<BaseActor> liveActors;
                liveActors.reserve(actorList.baseActors.size());

                for (const BaseActor& actor : actorList.baseActors)
                {
                    BaseActor* currentActor = serverCell->getActor(actor.refNum, actor.mpNum);
                    if (isClientActorControlUpdateAllowed(currentActor))
                        liveActors.push_back(actor);
                }

                actorList.baseActors = liveActors;
                actorList.count = static_cast<unsigned int>(actorList.baseActors.size());
                if (actorList.count == 0)
                    return;

                const bool hasCellAuthority = serverCell->hasAuthority(actorList.guid);

                // If the cell is loaded, regular actor movement may only cross adjacent exterior grids.
                // Door/interior transitions need an explicit follower flag or server-side script authority.
                std::vector<BaseActor> permittedActors;
                permittedActors.reserve(actorList.baseActors.size());

                for (const BaseActor& actor : actorList.baseActors)
                {
                    const bool canCrossCell = actor.isFollowerCellChange
                        || (hasCellAuthority && isAdjacentExteriorCellChange(actorList.cell, actor.cell));

                    if (canCrossCell)
                        permittedActors.push_back(actor);
                }

                actorList.baseActors = permittedActors;
                actorList.count = static_cast<unsigned int>(actorList.baseActors.size());

                if (actorList.count != 0)
                {
                    cacheCellChange(actorList);
                    isAccepted = true;
                }
            }
            // If the cell isn't loaded, the packet must be from dialogue or a script, so accept it
            else
            {
                cacheCellChange(actorList);
                isAccepted = true;
            }

            if (isAccepted)
            {
                ServerEvents::actorCellChange(player.getId(), actorList.cell.getDescription().c_str());

                sendCellChangeToLoaded(packet, actorList);
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORCELLCHANGE_HPP
