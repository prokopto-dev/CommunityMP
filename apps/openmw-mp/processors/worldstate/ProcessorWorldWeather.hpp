#ifndef OPENMW_PROCESSORWORLDWEATHER_HPP
#define OPENMW_PROCESSORWORLDWEATHER_HPP

#include "../WorldstateProcessor.hpp"

namespace mwmp
{
    class ProcessorWorldWeather : public WorldstateProcessor
    {
    public:
        ProcessorWorldWeather()
        {
            BPP_INIT(ID_WORLD_WEATHER)
        }

        void Do(WorldstatePacket &packet, Player &player, BaseWorldstate &worldstate) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            ServerEvents::playerEvent("OnWorldWeather", player.getId());
        }
    };
}

#endif //OPENMW_PROCESSORWORLDWEATHER_HPP
