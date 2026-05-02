#ifndef OPENMW_MWPHYSICS_IPHYSICSACTOR_H
#define OPENMW_MWPHYSICS_IPHYSICSACTOR_H

// Minimal abstraction over per-actor handles consumed by mwworld /
// mwmechanics. Mirrors what IPhysicsObject does for static colliders.
//
// Methods listed here are exactly the ones called on the result of
// IPhysicsBackend::getActor outside the mwphysics module — verified
// by grep. Bullet's MWPhysics::Actor implements this; Jolt's
// MWPhysics::JoltActor implements this. Phase 3 of the Jolt
// migration's character-fix plan replaces the concrete-type return
// of getActor with this abstract pointer so both backends can hand
// callers a usable handle.

#include <components/detournavigator/collisionshapetype.hpp>
#include <osg/Vec3f>

namespace MWPhysics
{
    class IPhysicsActor
    {
    public:
        virtual ~IPhysicsActor() = default;

        // Enable / disable internal (against world) and external
        // (against other actors) collision response.
        virtual void enableCollisionMode(bool collision) = 0;
        virtual bool getCollisionMode() const = 0;
        virtual void enableCollisionBody(bool collision) = 0;

        // Water-walk frame state — true if the actor was walking on
        // the water surface during the last simulation tick.
        virtual bool isWalkingOnWater() const = 0;

        // Collision shape type + half-extents drive navmesh agent
        // bounds and the gameplay's "is this a bipedal NPC" branch.
        virtual DetourNavigator::CollisionShapeType getCollisionShapeType() const = 0;
        virtual osg::Vec3f getHalfExtents() const = 0;

        // Active flag controls whether the simulator advances this
        // actor's character controller this frame. Mwbase::World uses
        // it to freeze NPCs in non-active cells.
        virtual void setActive(bool value) = 0;

        // Register a positional offset to apply during the next
        // simulation step. Called from World::moveObjectBy, scripted
        // teleports, and similar.
        virtual void adjustPosition(const osg::Vec3f& offset) = 0;
    };
}

#endif
