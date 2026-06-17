#ifndef OPENMW_PROCESSORWORLDTIME_HPP
#define OPENMW_PROCESSORWORLDTIME_HPP

#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwworld/globalvariablename.hpp>

#include <string_view>

#include "../WorldstateProcessor.hpp"

namespace mwmp
{
    class ProcessorWorldTime final: public WorldstateProcessor
    {
    public:
        ProcessorWorldTime()
        {
            BPP_INIT(ID_WORLD_TIME)
        }

        virtual void Do(WorldstatePacket &packet, Worldstate &worldstate)
        {
            MWBase::World *world = MWBase::Environment::get().getWorld();

            if (worldstate.time.hour != -1)
                world->setGlobalFloat(MWWorld::GlobalVariableName(std::string_view("gamehour")), worldstate.time.hour);

            if (worldstate.time.day != -1)
                world->setGlobalInt(MWWorld::GlobalVariableName(std::string_view("day")), worldstate.time.day);

            if (worldstate.time.month != -1)
                world->setGlobalInt(MWWorld::GlobalVariableName(std::string_view("month")), worldstate.time.month);

            if (worldstate.time.year != -1)
                world->setGlobalInt(MWWorld::GlobalVariableName(std::string_view("year")), worldstate.time.year);

            if (worldstate.time.timeScale != -1)
                world->setGlobalFloat(MWWorld::GlobalVariableName(std::string_view("timescale")), worldstate.time.timeScale);

            if (worldstate.time.daysPassed != -1)
                world->setGlobalInt(MWWorld::GlobalVariableName(std::string_view("dayspassed")), worldstate.time.daysPassed);
        }
    };
}



#endif //OPENMW_PROCESSORWORLDTIME_HPP

