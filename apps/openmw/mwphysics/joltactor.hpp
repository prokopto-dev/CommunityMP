#ifndef OPENMW_MWPHYSICS_JOLTACTOR_H
#define OPENMW_MWPHYSICS_JOLTACTOR_H

#include "physicsbackend.hpp"

#if OPENMW_PHYSICS_USES_JOLT

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <memory>

#include <osg/Vec3f>

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
    class JoltActor
    {
    public:
        JoltActor(const MWWorld::Ptr& ptr, const osg::Vec3f& halfExtents,
            const osg::Vec3f& position, JPH::PhysicsSystem& joltSystem);
        ~JoltActor();

        JoltActor(const JoltActor&) = delete;
        JoltActor& operator=(const JoltActor&) = delete;

        const MWWorld::Ptr& getPtr() const { return mPtr; }

        osg::Vec3f getHalfExtents() const { return mHalfExtents; }

        // World-space centre of the collision capsule.
        osg::Vec3f getPosition() const;

        // Phase 7d wires these. For now they return defaults so
        // callers (AI / camera) don't crash before phase 7 is done.
        bool isOnGround() const { return mIsOnGround; }

        JPH::CharacterVirtual* getCharacter() { return mCharacter.get(); }

    private:
        MWWorld::Ptr mPtr;
        osg::Vec3f mHalfExtents;
        std::unique_ptr<JPH::CharacterVirtual> mCharacter;

        // Cached per-frame state. Phase 7d keeps these in sync from
        // the Jolt character.
        bool mIsOnGround = false;
    };
}

#endif

#endif
