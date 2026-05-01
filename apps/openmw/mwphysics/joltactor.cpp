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

        // Slope ceiling: vanilla MW lets actors walk surfaces up to
        // ~46° before sliding (fSlopeBraking GMST). Phase 7f tunes
        // this against the real GMST; 45° is a sane phase-7a default.
        settings->mMaxSlopeAngle = 0.25f * JPH::JPH_PI;

        // Predictive contact distance: how far Jolt scans outside
        // the shape to find an upcoming wall. 0.1 cm is too tight at
        // MW's centimetre-scale; 5 cm avoids the "stuck on a corner"
        // failure mode without ghost collisions.
        settings->mPredictiveContactDistance = 5.0f;

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
}

#endif
