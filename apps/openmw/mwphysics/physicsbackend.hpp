#ifndef OPENMW_MWPHYSICS_PHYSICSBACKEND_H
#define OPENMW_MWPHYSICS_PHYSICSBACKEND_H

// Compile-time selection of the physics simulation backend.
//
// One of OPENMW_PHYSICS_BACKEND_BULLET / OPENMW_PHYSICS_BACKEND_JOLT is
// defined by the top-level CMakeLists based on the OPENMW_PHYSICS_BACKEND
// option (default: bullet). The two paths are mutually exclusive — there
// is no runtime toggle.
//
// Bullet stays linked unconditionally because components/detournavigator
// and components/bullethelpers consume btCollisionShape directly to feed
// Recast. When the Jolt backend is active, Bullet is used as a "data
// source" only (NIF -> btCollisionShape -> RecastMesh) while the active
// simulation runs on Jolt.

#if defined(OPENMW_PHYSICS_BACKEND_JOLT)
#define OPENMW_PHYSICS_USES_JOLT 1
#define OPENMW_PHYSICS_USES_BULLET 0
#elif defined(OPENMW_PHYSICS_BACKEND_BULLET)
#define OPENMW_PHYSICS_USES_JOLT 0
#define OPENMW_PHYSICS_USES_BULLET 1
#else
#error "No physics backend selected — define OPENMW_PHYSICS_BACKEND_BULLET or OPENMW_PHYSICS_BACKEND_JOLT"
#endif

#include <memory>

#include <osg/Group>
#include <osg/ref_ptr>

namespace Resource
{
    class ResourceSystem;
}

namespace MWPhysics
{
    class IPhysicsBackend;

    inline constexpr const char* physicsBackendName()
    {
#if OPENMW_PHYSICS_USES_JOLT
        return "Jolt";
#else
        return "Bullet";
#endif
    }

    // Single entry point for picking the runtime simulator. Defined
    // in physicsbackend.cpp so the header stays free of heavy
    // includes (PhysicsSystem / JoltPhysicsSystem). Returns
    // PhysicsSystem under bullet builds, JoltPhysicsSystem under
    // jolt builds.
    std::unique_ptr<IPhysicsBackend> makePhysicsBackend(
        Resource::ResourceSystem* resourceSystem, osg::ref_ptr<osg::Group> parentNode);
}

#endif
