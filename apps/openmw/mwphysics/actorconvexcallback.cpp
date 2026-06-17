#include "actorconvexcallback.hpp"
#include "collisiontype.hpp"
#include "contacttestwrapper.h"

#include <BulletCollision/CollisionDispatch/btCollisionObject.h>
#include <BulletCollision/NarrowPhaseCollision/btRaycastCallback.h>
#include <components/misc/convert.hpp>

#include "projectile.hpp"

namespace MWPhysics
{
    namespace
    {
        struct ActorOverlapTester : public btCollisionWorld::ContactResultCallback
        {
            bool mOverlapping = false;

            btScalar addSingleResult(btManifoldPoint& cp, const btCollisionObjectWrapper* /*colObj0Wrap*/,
                int /*partId0*/, int /*index0*/, const btCollisionObjectWrapper* /*colObj1Wrap*/, int /*partId1*/,
                int /*index1*/) override
            {
                if (cp.getDistance() <= 0.0f)
                    mOverlapping = true;
                return 1;
            }
        };

        class FrontFaceRayCallback : public btCollisionWorld::ClosestRayResultCallback
        {
        public:
            FrontFaceRayCallback(const btCollisionObject& me, const btCollisionObject& target, const btVector3& from,
                const btVector3& to)
                : btCollisionWorld::ClosestRayResultCallback(from, to)
                , mTarget(&target)
            {
                m_collisionFilterGroup = me.getBroadphaseHandle()->m_collisionFilterGroup;
                m_collisionFilterMask = me.getBroadphaseHandle()->m_collisionFilterMask;
                m_flags = btTriangleRaycastCallback::kF_FilterBackfaces;
            }

            bool needsCollision(btBroadphaseProxy* proxy) const override
            {
                return proxy->m_clientObject == mTarget
                    && btCollisionWorld::ClosestRayResultCallback::needsCollision(proxy);
            }

            btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult, bool normalInWorldSpace) override
            {
                if (rayResult.m_collisionObject != mTarget)
                    return 1;
                return btCollisionWorld::ClosestRayResultCallback::addSingleResult(rayResult, normalInWorldSpace);
            }

        private:
            const btCollisionObject* mTarget;
        };

        bool isWorldTriangleHit(const btCollisionWorld::LocalConvexResult& convexResult)
        {
            const btBroadphaseProxy* handle = convexResult.m_hitCollisionObject->getBroadphaseHandle();
            return handle != nullptr && handle->m_collisionFilterGroup == CollisionType_World
                && convexResult.m_localShapeInfo != nullptr && convexResult.m_localShapeInfo->m_shapePart >= 0
                && convexResult.m_localShapeInfo->m_triangleIndex >= 0;
        }

        bool hitsAuthoredFrontFace(const btCollisionWorld& world, const btCollisionObject& me,
            const btCollisionObject& target, const btVector3& hitPointWorld, const btVector3& movement)
        {
            const btScalar length = movement.length();
            if (length <= SIMD_EPSILON)
                return true;

            const btVector3 direction = movement / length;
            const btScalar probeDistance = btMax(btScalar(2.0), btMin(length, btScalar(8.0)));
            const btVector3 from = hitPointWorld - direction * probeDistance;
            const btVector3 to = hitPointWorld + direction * probeDistance;

            FrontFaceRayCallback callback(me, target, from, to);
            world.rayTest(from, to, callback);
            return callback.hasHit();
        }
    }

    btScalar ActorConvexCallback::addSingleResult(
        btCollisionWorld::LocalConvexResult& convexResult, bool normalInWorldSpace)
    {
        if (convexResult.m_hitCollisionObject == mMe)
            return 1;

        // override data for actor-actor collisions
        // vanilla Morrowind seems to make overlapping actors collide as though they are both cylinders with a diameter
        // of the distance between them For some reason this doesn't work as well as it should when using capsules, but
        // it still helps a lot.
        if (convexResult.m_hitCollisionObject->getBroadphaseHandle()->m_collisionFilterGroup == CollisionType_Actor)
        {
            ActorOverlapTester isOverlapping;
            // FIXME: This is absolutely terrible and bullet should feel terrible for not making contactPairTest
            // const-correct.
            ContactTestWrapper::contactPairTest(const_cast<btCollisionWorld*>(mWorld),
                const_cast<btCollisionObject*>(mMe), const_cast<btCollisionObject*>(convexResult.m_hitCollisionObject),
                isOverlapping);

            if (isOverlapping.mOverlapping)
            {
                auto originA = Misc::Convert::toOsg(mMe->getWorldTransform().getOrigin());
                auto originB = Misc::Convert::toOsg(convexResult.m_hitCollisionObject->getWorldTransform().getOrigin());
                osg::Vec3f motion = Misc::Convert::toOsg(mMotion);
                osg::Vec3f normal = (originA - originB);
                normal.z() = 0;
                normal.normalize();
                // only collide if horizontally moving towards the hit actor (note: the motion vector appears to be
                // inverted)
                // FIXME: This kinda screws with standing on actors that walk up slopes for some reason. Makes you fall
                // through them. It happens in vanilla Morrowind too, but much less often. I tried hunting down why but
                // couldn't figure it out. Possibly a stair stepping or ground ejection bug.
                if (normal * motion > 0.0f)
                {
                    convexResult.m_hitFraction = 0.0f;
                    convexResult.m_hitNormalLocal = Misc::Convert::toBullet(normal);
                    return ClosestConvexResultCallback::addSingleResult(convexResult, true);
                }
                else
                {
                    return 1;
                }
            }
        }
        if (convexResult.m_hitCollisionObject->getBroadphaseHandle()->m_collisionFilterGroup
            == CollisionType_Projectile)
        {
            auto* projectileHolder = static_cast<Projectile*>(convexResult.m_hitCollisionObject->getUserPointer());
            if (!projectileHolder->isActive())
                return 1;
            if (projectileHolder->isValidTarget(mMe))
                projectileHolder->hit(mMe, convexResult.m_hitPointLocal, convexResult.m_hitNormalLocal);
            return 1;
        }

        btVector3 hitNormalWorld;
        if (normalInWorldSpace)
            hitNormalWorld = convexResult.m_hitNormalLocal;
        else
        {
            /// need to transform normal into worldspace
            hitNormalWorld
                = convexResult.m_hitCollisionObject->getWorldTransform().getBasis() * convexResult.m_hitNormalLocal;
        }

        if (isWorldTriangleHit(convexResult)
            && !hitsAuthoredFrontFace(
                *mWorld, *mMe, *convexResult.m_hitCollisionObject, convexResult.m_hitPointLocal, -mMotion))
            return 1;

        // dot product of the motion vector against the collision contact normal
        btScalar dotCollision = mMotion.dot(hitNormalWorld);
        if (dotCollision <= mMinCollisionDot)
            return 1;

        return ClosestConvexResultCallback::addSingleResult(convexResult, normalInWorldSpace);
    }
}
