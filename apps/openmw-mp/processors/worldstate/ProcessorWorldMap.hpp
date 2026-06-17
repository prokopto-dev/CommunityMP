#ifndef OPENMW_PROCESSORWORLDMAP_HPP
#define OPENMW_PROCESSORWORLDMAP_HPP

#include "../WorldstateProcessor.hpp"

namespace mwmp
{
    class ProcessorWorldMap : public WorldstateProcessor
    {
    public:
        ProcessorWorldMap()
        {
            BPP_INIT(ID_WORLD_MAP)
        }

        void Do(WorldstatePacket &packet, Player &player, BaseWorldstate &worldstate) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            ServerEvents::playerEvent("OnWorldMap", player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORWORLDMAP_HPP
