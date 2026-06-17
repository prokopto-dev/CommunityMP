#include "joltactor.hpp"

#if OPENMW_PHYSICS_USES_JOLT

#include "joltphysicssystem.hpp" // JoltLayers::ACTOR_PROBE

#include <cstdlib>

#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <components/debug/debuglog.hpp>

namespace MWPhysics
{
    namespace
    {
        // Build the Jolt collision shape for an actor capsule from
        // OpenMW half-extents. Vanilla MW actors are oriented Z-up;
        // Jolt's CapsuleShape is along its local Y. We wrap in a
        // RotatedTranslated that maps Y -> Z so the actor stands
        // upright in the world.
        JPH::RefConst<JPH::Shape> buildActorShape(const osg::Vec3f& halfExtents)
        {
            // MW actor capsule:
            //   radius   = max(halfExtents.x, halfExtents.y)
            //   halfHeight (cylinder portion) = halfExtents.z - radius
            // Match what BulletNifLoader does for actors so the Jolt
            // shape matches the Bullet body byte-for-byte at the
            // collision-region level.
            const float radius = std::max(halfExtents.x(), halfExtents.y());
            const float halfHeight = std::max(0.001f, halfExtents.z() - radius);

            JPH::CapsuleShapeSettings capsule(halfHeight, radius);
            const auto baseResult = capsule.Create();
            if (baseResult.HasError())
                return nullptr;

            const JPH::Quat yToZ = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI);
            JPH::RotatedTranslatedShapeSettings rotated(JPH::Vec3::sZero(), yToZ, baseResult.Get().GetPtr());
            const auto rotResult = rotated.Create();
            if (rotResult.HasError())
                return nullptr;
            return rotResult.Get();
        }
    }

    JoltActor::JoltActor(const MWWorld::Ptr& ptr, const osg::Vec3f& halfExtents,
        const osg::Vec3f& position, JPH::PhysicsSystem& joltSystem)
        : mPtr(ptr)
        , mHalfExtents(halfExtents)
    {
        JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
        settings->mShape = buildActorShape(halfExtents);
        settings->mUp = JPH::Vec3(0.0f, 0.0f, 1.0f); // MW Z-up
        settings->mMass = 90.0f;                     // ~90 kg adult human, vanilla-ish

        // Shape sits half-height above the CV's reference position.
        // OpenMW gives us the actor's *feet* position, but Jolt's
        // CharacterVirtual centers its shape on its own position by
        // default — without this offset the capsule center is at
        // the feet, the bottom is underground, and the CV lifts the
        // body to settle, leaving the visual mesh hovering above
        // the ground by ~halfExtent.z. Mirrors how the Bullet path
        // adds mCollisionBox.mCenter (= (0, 0, halfExtent.z) for
        // auto-generated NPC capsules) to the feet position.
        settings->mShapeOffset = JPH::Vec3(0.0f, 0.0f, halfExtents.z());

        // Slope ceiling: vanilla MW lets actors walk surfaces up to
        // ~46° before sliding (fSlopeBraking GMST). Bumped to 50° so
        // user-mod stairs with slightly steep tessellation still
        // count as walkable instead of triggering the steep-slope
        // slide path before WalkStairs gets a chance.
        settings->mMaxSlopeAngle = (50.0f / 180.0f) * JPH::JPH_PI;

        // Predictive contact distance: how far Jolt scans outside
        // the shape to find an upcoming wall. Default 0.1 (m) is
        // microscopic at MW's "1 m ≈ 70 unit" scale. 50 MW units
        // (~70 cm) gives WalkStairs a full half-second of advance
        // notice at walking speed before the actor would collide
        // with a stair riser — plenty of time for the heuristic to
        // engage even if the actor is sprinting.
        settings->mPredictiveContactDistance = 50.0f;

        // Hit reduction merges contacts whose normals are within
        // ~2.5° by default. NIF stair geometry has small normal
        // variations from tessellation that should NOT be merged —
        // each unique step's riser is its own surface. Disable
        // hit reduction (-1) so the CV sees every step distinctly.
        settings->mHitReductionCosMaxAngle = -1.0f;

        // Character padding: how far Jolt tries to keep the shape
        // from geometry. Default 0.02 (m) → 1.4 MW units. Keep at
        // that scale so step-up doesn't ghost-collide.
        settings->mCharacterPadding = 1.5f;

        // Enhanced internal edge removal stays OFF: in practice
        // it interfered with the WalkStairs heuristic on MW NIF
        // tessellation (the very thing it's supposed to help)
        // because the merging logic kept the riser contacts but
        // dropped the tread edge that WalkStairs depends on for
        // step-down. With it disabled, distinct tread/riser
        // surfaces are seen as separate slope contacts which is
        // what WalkStairs actually wants.
        settings->mEnhancedInternalEdgeRemoval = false;

        // Inner body on the dedicated ACTOR_PROBE layer. Without one,
        // ray casts / sphere casts go through creatures (arrows fly
        // past). Putting the probes on their own layer lets the
        // pair filter keep ACTOR_PROBE × ACTOR_PROBE OFF — that was
        // the floating-NPC bug from `dce3a2f012`, where two CVs
        // mutually supported each other on their inner bodies.
        // The CharacterVirtual auto-filters its OWN inner body via
        // IgnoreSingleBodyFilterChained; per-actor body filters
        // additionally reject other actors' probes for non-player
        // CVs so NPCs don't collide-stack at spawn.
        settings->mInnerBodyShape = settings->mShape;
        settings->mInnerBodyLayer = JoltLayers::ACTOR_PROBE;

        mCharacter = std::make_unique<JPH::CharacterVirtual>(settings,
            JPH::RVec3(position.x(), position.y(), position.z()),
            JPH::Quat::sIdentity(), &joltSystem);
    }

    JoltActor::~JoltActor() = default;

    osg::Vec3f JoltActor::getPosition() const
    {
        if (!mCharacter)
            return osg::Vec3f();
        const JPH::RVec3 p = mCharacter->GetPosition();
        return osg::Vec3f(p.GetX(), p.GetY(), p.GetZ());
    }

    void JoltActor::setRotation(const osg::Quat& rot)
    {
        if (!mCharacter)
            return;
        mCharacter->SetRotation(JPH::Quat(rot.x(), rot.y(), rot.z(), rot.w()));
    }

    void JoltActor::updatePosition()
    {
        if (!mCharacter)
            return;
        const auto& pos = mPtr.getRefData().getPosition();
        const JPH::RVec3 prev = mCharacter->GetPosition();
        const JPH::RVec3 next(pos.pos[0], pos.pos[1], pos.pos[2]);
        mCharacter->SetPosition(next);
        mInertiaZ = 0.0f; // teleports clear the fall accumulator
        mNeedsGroundSnap = true;
        // Phase-A teleport diagnostic. Gated on OPENMW_JOLT_TRACE so
        // it stays quiet in normal play, only fires when we're
        // actively chasing a "teleport leaves you in place" report.
        static const bool sTrace = []() {
            const char* env = std::getenv("OPENMW_JOLT_TRACE");
            return env != nullptr && env[0] != '0' && env[0] != '\0';
        }();
        if (sTrace)
        {
            Log(Debug::Info) << "[jolt-teleport] " << mPtr.getCellRef().getRefId().toDebugString()
                << " from=(" << prev.GetX() << "," << prev.GetY() << "," << prev.GetZ() << ")"
                << " to=(" << next.GetX() << "," << next.GetY() << "," << next.GetZ() << ")"
                << " delta=" << (next - prev).Length();
        }
    }

    void JoltActor::adjustPosition(const osg::Vec3f& offset)
    {
        // Apply the offset directly to the CharacterVirtual; mirrors
        // MWPhysics::Actor::adjustPosition + applyOffsetChange in the
        // Bullet path. Used by World::moveObjectBy and scripted
        // teleports — the simulator picks the new position up on the
        // next ExtendedUpdate.
        if (!mCharacter)
            return;
        const JPH::RVec3 cur = mCharacter->GetPosition();
        mCharacter->SetPosition(JPH::RVec3(
            cur.GetX() + offset.x(),
            cur.GetY() + offset.y(),
            cur.GetZ() + offset.z()));
    }

    void JoltActor::refreshState()
    {
        if (!mCharacter)
            return;
        const auto gs = mCharacter->GetGroundState();
        mIsOnGround = (gs == JPH::CharacterVirtual::EGroundState::OnGround);
        mIsOnSlope = (gs == JPH::CharacterVirtual::EGroundState::OnSteepGround);
    }
}

#endif
