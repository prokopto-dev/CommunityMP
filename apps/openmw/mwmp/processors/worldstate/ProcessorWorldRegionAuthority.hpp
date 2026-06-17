#ifndef OPENMW_PROCESSORWORLDREGIONAUTHORITY_HPP
#define OPENMW_PROCESSORWORLDREGIONAUTHORITY_HPP

#include <apps/openmw/mwbase/world.hpp>
#include <components/esm/refid.hpp>

#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorWorldRegionAuthority final: public WorldstateProcessor
    {
    public:
        ProcessorWorldRegionAuthority()
        {
            BPP_INIT(ID_WORLD_REGION_AUTHORITY)
        }

        virtual void Do(WorldstatePacket &packet, Worldstate &worldstate)
        {
            MWBase::World *world = MWBase::Environment::get().getWorld();

            const ESM::RefId playerRegion = world->getPlayerPtr().getCell()->getCell()->getRegion();
            if (!worldstate.authorityRegion.empty() && playerRegion == ESM::RefId::stringRefId(worldstate.authorityRegion))
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s about %s", strPacketID.c_str(), worldstate.authorityRegion.c_str());

                if (isLocal())
                {
                    LOG_APPEND(TimedLog::LOG_INFO, "- The new region authority is me");
                    worldstate.setWeatherAuthority(true);
                    worldstate.sendWeather(worldstate.authorityRegion, world->getCurrentWeatherScriptId(),
                        world->getNextWeatherScriptId(), world->getQueuedWeatherScriptId(), world->getWeatherTransition());
                }
                else
                {
                    worldstate.setWeatherAuthority(false);

                    BasePlayer *player = PlayerList::getPlayer(guid);

                    if (player != 0)
                        LOG_APPEND(TimedLog::LOG_INFO, "- The new region authority is %s", player->npc.mName.c_str());
                }
            }
        }
    };
}

#endif //OPENMW_PROCESSORWORLDREGIONAUTHORITY_HPP

