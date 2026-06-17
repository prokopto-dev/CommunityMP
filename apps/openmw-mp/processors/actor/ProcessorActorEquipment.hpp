#ifndef OPENMW_PROCESSORACTOREQUIPMENT_HPP
#define OPENMW_PROCESSORACTOREQUIPMENT_HPP

#include "../ActorProcessor.hpp"
#include "ActorSequenceCoalescing.hpp"

namespace mwmp
{
    class ProcessorActorEquipment : public ActorProcessor
    {
    public:
        ProcessorActorEquipment()
        {
            BPP_INIT(ID_ACTOR_EQUIPMENT)
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            // Send only to players who have the cell loaded
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell != nullptr && serverCell->hasAuthority(actorList.guid))
            {
                if (!filterActorEquipmentToServerAccepted(serverCell, actorList))
                    return;

                serverCell->readActorList(packetID, &actorList);

                Script::Call<Script::CallbackIdentity("OnActorEquipment")>(player.getId(), actorList.cell.getDescription().c_str());

                serverCell->sendToLoaded(&packet, &actorList);
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTOREQUIPMENT_HPP
