#include "joltactor.hpp"

#if OPENMW_PHYSICS_USES_JOLT

#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

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
        // ~46° before sliding (fSlopeBraking GMST). Phase 7f tunes
        // this against the real GMST; 45° is a sane phase-7a default.
        settings->mMaxSlopeAngle = 0.25f * JPH::JPH_PI;

        // Predictive contact distance: how far Jolt scans outside
        // the shape to find an upcoming wall. 0.1 cm is too tight at
        // MW's centimetre-scale; 5 cm avoids the "stuck on a corner"
        // failure mode without ghost collisions.
        settings->mPredictiveContactDistance = 5.0f;

        // No inner body for now: when set, every actor's CV-internal
        // kinematic body sits in the world at the actor's position
        // and shows up to OTHER characters' collision queries as a
        // solid obstacle. Two NPCs spawned near each other end up
        // mutually supported, floating in mid-air. The downside of
        // dropping the inner body is that ray casts / sphere casts
        // no longer hit characters — phase 8 will reintroduce one
        // in a dedicated broadphase layer that's filtered out of
        // the CharacterVirtual's collision query.

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
        mCharacter->SetPosition(JPH::RVec3(pos.pos[0], pos.pos[1], pos.pos[2]));
        mInertiaZ = 0.0f; // teleports clear the fall accumulator
        mNeedsGroundSnap = true;
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
