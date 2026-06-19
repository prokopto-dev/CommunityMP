#include "serversimulationevents.hpp"

#include "serversimulationmode.hpp"

#include <mutex>
#include <utility>

namespace OMW
{
    namespace
    {
        std::mutex gServerSimulationCombatEventsMutex;
        std::vector<ServerSimulationCombatEvent> gServerSimulationCombatEvents;
    }

    void queueServerSimulationCombatEvent(ServerSimulationCombatEvent event)
    {
        if (!isServerSimulationModeActive())
            return;

        std::lock_guard lock(gServerSimulationCombatEventsMutex);
        gServerSimulationCombatEvents.push_back(std::move(event));
    }

    std::vector<ServerSimulationCombatEvent> drainServerSimulationCombatEvents()
    {
        std::lock_guard lock(gServerSimulationCombatEventsMutex);
        std::vector<ServerSimulationCombatEvent> events;
        events.swap(gServerSimulationCombatEvents);
        return events;
    }

    void clearServerSimulationCombatEvents()
    {
        std::lock_guard lock(gServerSimulationCombatEventsMutex);
        gServerSimulationCombatEvents.clear();
    }
}
