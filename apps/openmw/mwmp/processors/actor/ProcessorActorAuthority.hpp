#ifndef OPENMW_PROCESSORACTORAUTHORITY_HPP
#define OPENMW_PROCESSORACTORAUTHORITY_HPP


#include "../ActorProcessor.hpp"
#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwbase/world.hpp"
#include "apps/openmw/mwmp/Main.hpp"
#include "apps/openmw/mwmp/CellController.hpp"

namespace mwmp
{
    class ProcessorActorAuthority final: public ActorProcessor
    {
    public:
        ProcessorActorAuthority()
        {
            BPP_INIT(ID_ACTOR_AUTHORITY)
        }

        virtual void Do(ActorPacket &packet, ActorList &actorList)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s about %s", strPacketID.c_str(),
                actorList.cell.getDescription().c_str());
            mwmp::CellController *cellController = Main::get().getCellController();

            // Never initialize LocalActors in a cell that is no longer loaded, if the server's packet arrived too late
            if (cellController->isActiveWorldCell(actorList.cell))
            {
                cellController->initializeCell(actorList.cell);

                if (isLocal())
                {
                    LOG_APPEND(TimedLog::LOG_INFO, "- The new authority is me");
                }
                else
                {
                    BasePlayer *player = PlayerList::getPlayer(guid);

                    if (player != 0)
                        LOG_APPEND(TimedLog::LOG_INFO, "- The new authority is %s", player->npc.mName.c_str());
                }

                cellController->applyActorAuthority(actorList.cell, guid);
            }
            else
            {
                cellController->queueActorAuthority(actorList.cell, guid);
                LOG_APPEND(TimedLog::LOG_INFO, "- Queued it because that cell isn't loaded yet");
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORAUTHORITY_HPP

