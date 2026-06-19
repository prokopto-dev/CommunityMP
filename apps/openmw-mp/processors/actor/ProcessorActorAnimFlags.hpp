#ifndef OPENMW_PROCESSORACTORANIMFLAGS_HPP
#define OPENMW_PROCESSORACTORANIMFLAGS_HPP

#include "../ActorProcessor.hpp"
#include "ActorSequenceCoalescing.hpp"

namespace mwmp
{
    class ProcessorActorAnimFlags : public ActorProcessor
    {
    public:
        ProcessorActorAnimFlags()
        {
            BPP_INIT(ID_ACTOR_ANIM_FLAGS)
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            // Send only to players who have the cell loaded
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);
            if (rejectClientActorPacketForServerOwnedCell(serverCell, actorList, "ID_ACTOR_ANIM_FLAGS"))
                return;

            if (serverCell != nullptr && serverCell->hasAuthority(actorList.guid))
            {
                std::vector<BaseActor> correctionActors;
                correctionActors.reserve(actorList.baseActors.size());
                BaseActorList correctionList = actorList;

                if (!filterActorAnimFlagsToServerAccepted(serverCell, actorList, &correctionActors))
                {
                    if (!correctionActors.empty())
                    {
                        correctionList.baseActors = correctionActors;
                        correctionList.count = static_cast<unsigned int>(correctionList.baseActors.size());
                        packet.setActorList(&correctionList);
                        packet.SendWithReliability(actorList.guid, PacketReliability::ReliableOrdered);
                    }
                    return;
                }

                if (!correctionActors.empty())
                {
                    correctionList.baseActors = correctionActors;
                    correctionList.count = static_cast<unsigned int>(correctionList.baseActors.size());
                    packet.setActorList(&correctionList);
                    packet.SendWithReliability(actorList.guid, PacketReliability::ReliableOrdered);
                }

                serverCell->readActorList(packetID, &actorList);
                serverCell->sendToLoaded(&packet, &actorList);
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORANIMFLAGS_HPP
