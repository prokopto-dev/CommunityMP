#ifndef OPENMW_MWPHYSICS_JOLTACTOR_H
#define OPENMW_MWPHYSICS_JOLTACTOR_H

#include "physicsbackend.hpp"

#if OPENMW_PHYSICS_USES_JOLT

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <memory>

#include <osg/Quat>
#include <osg/Vec3f>

#include <components/detournavigator/collisionshapetype.hpp>

#include "iphysicsactor.hpp"

#include "../mwworld/ptr.hpp"

namespace MWPhysics
{
    // Per-actor wrapper around JPH::CharacterVirtual. Mirrors the
    // role of MWPhysics::Actor in the Bullet path: owns the
    // collision shape (a capsule built from half-extents), stores
    // the per-frame movement queue, and exposes positional state to
    // the rest of the engine.
    //
    // Phase 7a (this commit): construction + identity. Phase 7b will
    // wire the movement queue and ExtendedUpdate; phase 7f tunes
    // slope/water/stuck behaviour to match vanilla MovementSolver.
    class JoltActor final : public IPhysicsActor
    {
    public:
        JoltActor(const MWWorld::Ptr& ptr, const osg::Vec3f& halfExtents,
            const osg::Vec3f& position, JPH::PhysicsSystem& joltSystem);
        ~JoltActor() override;

        JoltActor(const JoltActor&) = delete;
        JoltActor& operator=(const JoltActor&) = delete;

        const MWWorld::Ptr& getPtr() const { return mPtr; }

        // World-space centre of the collision capsule.
        osg::Vec3f getPosition() const;

        bool isOnGround() const { return mIsOnGround; }
        bool isOnSlope() const { return mIsOnSlope; }
        bool isActive() const { return mActive; }

        JPH::CharacterVirtual* getCharacter() { return mCharacter.get(); }

        // Rotate the underlying CharacterVirtual. Called by
        // JoltPhysicsSystem::updateRotation when gameplay turns the
        // actor (mouse-look on the player, AI turn on NPCs).
        void setRotation(const osg::Quat& rot);

        // Re-snap the CharacterVirtual to ptr.refData.position.
        // Used by scripted teleports and the world's place / move-by
        // paths. Resets the vertical inertia accumulator so the actor
        // doesn't carry stale fall speed across a teleport.
        void updatePosition();

        // Vertical inertia accumulator. Mirrors MWPhysics::Actor::mInertia
        // semantics: gravity adds to it each step, lands reset it,
        // jump impulses overwrite it. Kept separate from per-frame
        // input so horizontal motion doesn't drag vertical state.
        float getInertiaZ() const { return mInertiaZ; }
        void setInertiaZ(float z) { mInertiaZ = z; }

        // Refresh cached state from the underlying CharacterVirtual.
        // Called after every ExtendedUpdate by JoltPhysicsSystem so
        // const accessors (isOnGround / getPosition) don't have to
        // round-trip into Jolt.
        void refreshState();

        // After updatePosition (script teleport, cell change, place),
        // request that the next ExtendedUpdate use a generous
        // stick-to-floor distance so the CV snaps onto the floor even
        // if the spawn point sits noticeably above it (door entries
        // are often a capsule-radius above the floor mesh) or if the
        // surrounding cell shapes were registered just before the
        // step. Without this, the CV starts free-falling and on
        // interior cells with water it lands in the water sensor.
        bool consumeGroundSnapRequest()
        {
            const bool v = mNeedsGroundSnap;
            mNeedsGroundSnap = false;
            return v;
        }

        // ----- IPhysicsActor ------------------------------------------
        void enableCollisionMode(bool collision) override { mInternalCollision = collision; }
        bool getCollisionMode() const override { return mInternalCollision; }
        void enableCollisionBody(bool collision) override { mExternalCollision = collision; }
        bool isWalkingOnWater() const override { return mWalkingOnWater; }
        DetourNavigator::CollisionShapeType getCollisionShapeType() const override
        {
            // BoundingBox is the shape MW uses for actor capsules; the
            // Jolt path doesn't yet support the cylinder/aabb variants.
            return DetourNavigator::CollisionShapeType::Aabb;
        }
        osg::Vec3f getHalfExtents() const override { return mHalfExtents; }
        void setActive(bool value) override { mActive = value; }
        void adjustPosition(const osg::Vec3f& offset) override;

    private:
        MWWorld::Ptr mPtr;
        osg::Vec3f mHalfExtents;
        std::unique_ptr<JPH::CharacterVirtual> mCharacter;

        // Cached per-frame state. Phase 7d keeps these in sync from
        // the Jolt character.
        bool mIsOnGround = false;
        bool mIsOnSlope = false;
        float mInertiaZ = 0.0f;

        // IPhysicsActor flags. Default-on so the actor behaves like a
        // freshly-spawned vanilla MW NPC on the Bullet path.
        bool mInternalCollision = true;
        bool mExternalCollision = true;
        bool mWalkingOnWater = false;
        bool mActive = true;
        // Spawn counts as a teleport — request a snap-to-floor on
        // the first step so newly-added actors don't free-fall a
        // few frames before settling.
        bool mNeedsGroundSnap = true;
    };
}

#endif

#endif
