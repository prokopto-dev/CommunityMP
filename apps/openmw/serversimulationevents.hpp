#ifndef OPENMW_SERVERSIMULATIONEVENTS_HPP
#define OPENMW_SERVERSIMULATIONEVENTS_HPP

#include <vector>

#include <components/openmw-mp/Base/BaseStructs.hpp>

#include "mwworld/ptr.hpp"

namespace OMW
{
    struct ServerSimulationCombatEvent
    {
        MWWorld::Ptr attacker;
        MWWorld::Ptr victim;
        MWWorld::Ptr weapon;
        MWWorld::Ptr projectile;
        mwmp::Attack attack;
    };

    void queueServerSimulationCombatEvent(ServerSimulationCombatEvent event);
    std::vector<ServerSimulationCombatEvent> drainServerSimulationCombatEvents();
    void clearServerSimulationCombatEvents();
}

#endif // OPENMW_SERVERSIMULATIONEVENTS_HPP
