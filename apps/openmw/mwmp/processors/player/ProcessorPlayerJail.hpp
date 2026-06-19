#ifndef OPENMW_PROCESSORPLAYERJAIL_HPP
#define OPENMW_PROCESSORPLAYERJAIL_HPP

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwbase/world.hpp"
#include "apps/openmw/mwclass/npc.hpp"
#include "apps/openmw/mwgui/windowmanagerimp.hpp"
#include "apps/openmw/mwmechanics/npcstats.hpp"
#include "apps/openmw/mwworld/class.hpp"

#include "../PlayerProcessor.hpp"
#include "apps/openmw/mwmp/LocalPlayer.hpp"
#include "apps/openmw/mwmp/Main.hpp"
#include "apps/openmw/mwmp/Networking.hpp"

namespace mwmp
{
    class ProcessorPlayerJail final: public PlayerProcessor
    {
    public:
        ProcessorPlayerJail()
        {
            BPP_INIT(ID_PLAYER_JAIL)
        }

        virtual void Do(PlayerPacket &packet, BasePlayer *player)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_JAIL from server");
            
            if (isLocal())
            {
                MWWorld::Ptr ptrPlayer = MWBase::Environment::get().getWorld()->getPlayerPtr();
                if (!ptrPlayer.isEmpty())
                {
                    MWMechanics::NpcStats& npcStats = ptrPlayer.getClass().getNpcStats(ptrPlayer);
                    const bool hadBounty = npcStats.getBounty() > 0;
                    npcStats.setBounty(0);
                    MWBase::Environment::get().getWorld()->confiscateStolenItems(ptrPlayer);

                    if (mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer())
                    {
                        localPlayer->updateBounty(true);
                        localPlayer->updateInventory(true);
                        localPlayer->updateEquipment(true);
                    }

                    if (hadBounty)
                        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                            "Cleared local bounty and confiscated stolen goods for multiplayer jail");
                }

                // Apply death penalties
                if (player->jailDays > 0)
                {
                    MWBase::Environment::get().getWindowManager()->goToJail(player->jailDays);
                }
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERJAIL_HPP

