#ifndef OPENMW_SERVERSIMULATIONMODE_HPP
#define OPENMW_SERVERSIMULATIONMODE_HPP

#include <atomic>

namespace OMW
{
    inline std::atomic_bool gServerSimulationModeActive{ false };

    inline void setServerSimulationModeActive(bool active)
    {
        gServerSimulationModeActive.store(active, std::memory_order_relaxed);
    }

    inline bool isServerSimulationModeActive()
    {
        return gServerSimulationModeActive.load(std::memory_order_relaxed);
    }
}

#endif // OPENMW_SERVERSIMULATIONMODE_HPP
