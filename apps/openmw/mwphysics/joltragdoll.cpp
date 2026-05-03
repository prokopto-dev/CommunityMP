#include "joltragdoll.hpp"

#if OPENMW_PHYSICS_USES_JOLT && OPENMW_JOLT_RAGDOLL

#include <algorithm>

#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Skeleton/Skeleton.h>

#include "joltphysicssystem.hpp" // JoltLayers::ACTOR_PROBE
#include <components/debug/debuglog.hpp>

namespace MWPhysics
{
    // Hide the JPH::Ref<JPH::Ragdoll> in a forward-declared holder so
    // joltragdoll.hpp can stay free of Jolt headers (it's pulled into
    // joltphysicssystem.hpp via the unique_ptr's destructor).
    struct JoltRagdoll::RagdollHolder
    {
        JPH::Ref<JPH::Ragdoll> mRef;
    };

    namespace
    {
        // Phase 1 ragdoll skeleton: 7 bodies in a fixed topology that
        // approximates a humanoid silhouette without reading the
        // NIF. Sizes scale off the actor's overall half-extents so a
        // tall/short character produces a tall/short ragdoll. Phase
        // 2 replaces this with NIF-driven rigging.
        //
        //          head
        //           |
        //         spine
        //         /   \
        //        L     R   upper arms (children of spine)
        //         |
        //       pelvis (child of spine)
        //         /   \
        //        L     R   thighs (children of pelvis)
        //
        // Single segment per limb keeps the body count low for the
        // spike. Phase 2 adds elbows / knees once we have real bones.
        enum BodyIndex
        {
            kPelvis = 0,
            kSpine,
            kHead,
            kArmL,
            kArmR,
            kLegL,
            kLegR,
            kBodyCount,
        };

        struct BoneSpec
        {
            const char* name;
            int parent;       // -1 for root
            float radius;     // capsule radius, fraction of halfExt.x
            float length;     // capsule length, fraction of halfExt.z
            osg::Vec3f offset; // body-space offset from parent, fraction of halfExt
        };

        constexpr BoneSpec kBones[kBodyCount] = {
            // name      parent    radius  length  offset(x,y,z)
            { "pelvis",  -1,       0.55f,  0.30f,  {  0.0f, 0.0f,  0.30f } },
            { "spine",    kPelvis, 0.50f,  0.40f,  {  0.0f, 0.0f,  0.50f } },
            { "head",     kSpine,  0.35f,  0.20f,  {  0.0f, 0.0f,  0.40f } },
            { "armL",     kSpine,  0.20f,  0.45f,  {  0.45f, 0.0f, 0.20f } },
            { "armR",     kSpine,  0.20f,  0.45f,  { -0.45f, 0.0f, 0.20f } },
            { "legL",     kPelvis, 0.25f,  0.55f,  {  0.25f, 0.0f, -0.50f } },
            { "legR",     kPelvis, 0.25f,  0.55f,  { -0.25f, 0.0f, -0.50f } },
        };

        // Mass distribution for a 90 kg humanoid. Sums to 90.
        constexpr float kMass[kBodyCount] = {
            18.0f, // pelvis  (lower torso)
            22.0f, // spine   (upper torso + organs)
            5.0f,  // head
            5.0f,  // armL
            5.0f,  // armR
            17.5f, // legL
            17.5f, // legR
        };
    }

    JoltRagdoll::JoltRagdoll(const MWWorld::Ptr& ptr, const osg::Vec3f& halfExtents,
        const osg::Vec3f& position, const osg::Quat& /*rotation*/,
        JPH::PhysicsSystem& joltSystem)
        : mPtr(ptr)
        , mJoltSystem(&joltSystem)
        , mRagdoll(std::make_unique<RagdollHolder>())
    {
        // Phase 1 stub: log that we'd build a ragdoll for this actor.
        // Real construction (RagdollSettings + bodies + constraints)
        // lands in phase 2 once we have the bone reading from the
        // NIF skeleton wired up. Keeping this as a stub means the
        // OPENMW_JOLT_RAGDOLL=ON build compiles end-to-end, the
        // JoltPhysicsSystem can hold instances, and we have the
        // dispatch path ready to populate.
        Log(Debug::Info) << "[jolt-ragdoll] would spawn for "
            << ptr.getCellRef().getRefId().toDebugString()
            << " halfExt=(" << halfExtents.x() << "," << halfExtents.y() << "," << halfExtents.z() << ")"
            << " pos=(" << position.x() << "," << position.y() << "," << position.z() << ")"
            << " bones=" << kBodyCount;
        (void)mJoltSystem;
        (void)kBones;
        (void)kMass;
    }

    JoltRagdoll::~JoltRagdoll() = default;

    void JoltRagdoll::postStep()
    {
        // Phase 1 stub: until we actually create bodies in phase 2,
        // there's nothing to integrate stability over. Force-mark
        // stable on the first call so the JoltPhysicsSystem retires
        // the ragdoll immediately. This keeps the dispatch path
        // exercised end-to-end without leaving inert ragdolls in
        // the simulator.
        mStableFrames = sStableFrames;
    }
}

#endif
