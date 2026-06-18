#ifndef COMMUNITYMP_OPENMWSERVERSIMULATIONHOST_HPP
#define COMMUNITYMP_OPENMWSERVERSIMULATIONHOST_HPP

#include <memory>

#include "../openmw-mp/SimulationRuntime.hpp"

namespace communitymp
{
    std::unique_ptr<mwmp::SimulationRuntime> createOpenMwServerSimulationRuntime();
}

#endif // COMMUNITYMP_OPENMWSERVERSIMULATIONHOST_HPP
