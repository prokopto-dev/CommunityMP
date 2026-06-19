#ifndef OPENMW_PROCESSORUSERDISCONNECTED_HPP
#define OPENMW_PROCESSORUSERDISCONNECTED_HPP


#include "../PlayerProcessor.hpp"
#include <components/openmw-mp/Utils.hpp>
#include <components/openmw-mp/Transport/PacketIdentity.hpp>
#include <apps/openmw/mwbase/environment.hpp>
#include "apps/openmw/mwstate/statemanagerimp.hpp"

namespace mwmp
{
    class ProcessorUserDisconnected final: public PlayerProcessor
    {
    public:
        ProcessorUserDisconnected()
        {
            BPP_INIT(ID_USER_DISCONNECTED)
            avoidReading = true;
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            static_cast<void>(packet);
            if (isLocal())
                MWBase::Environment::get().getStateManager()->requestQuit();
            else
            {
                PlayerProcessor::clearPendingPacketsForPlayer(guid);

                mwmp::LocalPlayer *localPlayer = mwmp::Main::get().getLocalPlayer();

                for (std::vector<PacketGuid>::iterator iter = localPlayer->alliedPlayers.begin(); iter != localPlayer->alliedPlayers.end(); )
                {
                    if (*iter == guid)
                    {
                        DedicatedPlayer *dedicatedPlayer = PlayerList::getPlayer(guid);
                        if (dedicatedPlayer != nullptr)
                            LOG_APPEND(TimedLog::LOG_INFO, "- Deleting %s from our allied players",
                                dedicatedPlayer->npc.mName.c_str());
                        else
                            LOG_APPEND(TimedLog::LOG_INFO, "- Deleting stale guid %s from our allied players",
                                packetGuidToString(guid).c_str());
                        iter = localPlayer->alliedPlayers.erase(iter);
                    }
                    else
                        ++iter;
                }

                if (player == nullptr)
                    LOG_APPEND(TimedLog::LOG_INFO, "- Deleting stale remote avatar for disconnected guid %s",
                        packetGuidToString(guid).c_str());

                PlayerList::deletePlayer(guid);
            }
        }
    };
}

#endif //OPENMW_PROCESSORUSERDISCONNECTED_HPP

