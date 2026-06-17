#ifndef OPENMW_MWPHYSICS_JOLTRAGDOLL_H
#define OPENMW_MWPHYSICS_JOLTRAGDOLL_H

#include "physicsbackend.hpp"

#if OPENMW_PHYSICS_USES_JOLT && OPENMW_JOLT_RAGDOLL

#include <memory>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <osg/Quat>
#include <osg/Vec3f>

#include "../mwworld/ptr.hpp"

namespace JPH
{
    class PhysicsSystem;
    class Ragdoll;
    class RagdollSettings;
}

namespace MWPhysics
{
    // Phase 1 (spike) of docs/jolt-ragdoll-plan.md: build a minimal
    // ragdoll for a single actor and simulate its fall, no rigging
    // from the NIF skeleton yet, no pose sync back to OSG. Just a
    // short articulated chain (pelvis → spine → head, plus the four
    // limbs) using the actor's overall half-extents to size the
    // capsules. The point of this phase is to confirm Jolt produces
    // a believable settle with our existing layer / filter setup
    // before we invest in NIF-skeleton driven rigging (phase 2).
    //
    // Lifecycle:
    //   - Constructed from JoltPhysicsSystem when a death is detected
    //     (death detection itself is gated behind OPENMW_JOLT_RAGDOLL
    //     at the call site; phase 3 wires the gameplay event).
    //   - Bodies + constraints registered into the shared Jolt system
    //     on construction; removed in the destructor.
    //   - The hosting JoltPhysicsSystem owns the unique_ptr and ticks
    //     stepStability() each frame to detect when the ragdoll has
    //     come to rest, at which point bodies switch to Static and
    //     the structure can be retired (phase 5).
    class JoltRagdoll
    {
    public:
        JoltRagdoll(const MWWorld::Ptr& ptr, const osg::Vec3f& halfExtents,
            const osg::Vec3f& position, const osg::Quat& rotation,
            JPH::PhysicsSystem& joltSystem);
        ~JoltRagdoll();

        JoltRagdoll(const JoltRagdoll&) = delete;
        JoltRagdoll& operator=(const JoltRagdoll&) = delete;

        const MWWorld::Ptr& getPtr() const { return mPtr; }

        // Returns true when every body's linear + angular velocity
        // has been below the sleep thresholds for at least
        // sStableFrames consecutive ticks. Once true, the caller
        // should retire the ragdoll (drop the unique_ptr) — Jolt
        // would happily keep simulating an inert pile of bodies but
        // there's no point spending broadphase cycles on it.
        bool isStable() const { return mStableFrames >= sStableFrames; }

        // One physics step. Updates internal stability tracking.
        // Caller decides whether to call this every frame or skip
        // already-stable ragdolls.
        void postStep();

    private:
        // ~2 s @ 60 Hz of "no significant motion" before we declare
        // the ragdoll settled. Tuneable; large enough to ride out the
        // small jitter that comes out of constraint relaxation.
        static constexpr int sStableFrames = 120;
        static constexpr float sLinearVelEpsilon = 0.5f;  // Jolt units
        static constexpr float sAngularVelEpsilon = 0.1f; // rad/s

        MWWorld::Ptr mPtr;
        JPH::PhysicsSystem* mJoltSystem;
        std::unique_ptr<JPH::RagdollSettings> mSettings;
        // Held by Ref because Jolt's Ragdoll inherits from RefCounted
        // and the Add/Remove helpers expect that ownership pattern.
        // Forward-declared here to keep the header lean — concrete
        // smart-pointer typedef lives in the .cpp.
        struct RagdollHolder;
        std::unique_ptr<RagdollHolder> mRagdoll;
        int mStableFrames = 0;
    };
}

#endif // OPENMW_PHYSICS_USES_JOLT && OPENMW_JOLT_RAGDOLL

#endif
