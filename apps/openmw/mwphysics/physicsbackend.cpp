#include "physicsbackend.hpp"

#include "iphysicsbackend.hpp"

#if OPENMW_PHYSICS_USES_JOLT
#include "joltphysicssystem.hpp"
#else
#include "physicssystem.hpp"
#endif

namespace MWPhysics
{
    std::unique_ptr<IPhysicsBackend> makePhysicsBackend(
        Resource::ResourceSystem* resourceSystem, osg::ref_ptr<osg::Group> parentNode)
    {
#if OPENMW_PHYSICS_USES_JOLT
        return std::make_unique<JoltPhysicsSystem>(resourceSystem, std::move(parentNode));
#else
        return std::make_unique<PhysicsSystem>(resourceSystem, std::move(parentNode));
#endif
    }
}
