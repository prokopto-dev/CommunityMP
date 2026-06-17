#ifndef OPENMW_MWPHYSICS_PROJECTILE_H
#define OPENMW_MWPHYSICS_PROJECTILE_H

#include <atomic>
#include <memory>
#include <mutex>

#include <LinearMath/btVector3.h>

#include "iprojectile.hpp"
#include "ptrholder.hpp"

class btCollisionObject;
class btCollisionShape;
class btConvexShape;

namespace osg
{
    class Vec3f;
}

namespace MWPhysics
{
    class PhysicsTaskScheduler;
    class PhysicsSystem;

    class Projectile final : public PtrHolder, public IProjectile
    {
    public:
        Projectile(const MWWorld::Ptr& caster, const osg::Vec3f& position, float radius,
            PhysicsTaskScheduler* scheduler, PhysicsSystem* physicssystem);
        ~Projectile() override;

        btConvexShape* getConvexShape() const { return mConvexShape; }

        void updateCollisionObjectPosition();

        bool isActive() const override { return mActive.load(std::memory_order_acquire); }

        MWWorld::Ptr getTarget() const override;

        MWWorld::Ptr getCaster() const override;
        void setCaster(const MWWorld::Ptr& caster) override;
        const btCollisionObject* getCasterCollisionObject() const { return mCasterColObj; }

        void setHitWater() { mHitWater = true; }

        bool getHitWater() const override { return mHitWater; }

        void hit(const btCollisionObject* target, btVector3 pos, btVector3 normal);

        void setValidTargets(const std::vector<MWWorld::Ptr>& targets) override;
        bool isValidTarget(const btCollisionObject* target) const;

        void setVelocity(osg::Vec3f velocity) override { PtrHolder::setVelocity(velocity); }
        osg::Vec3f getSimulationPosition() const override { return PtrHolder::getSimulationPosition(); }
        osg::Vec3f getHitPosition() const override;

    private:
        std::unique_ptr<btCollisionShape> mShape;
        btConvexShape* mConvexShape;

        bool mHitWater;
        std::atomic<bool> mActive;
        MWWorld::Ptr mCaster;
        const btCollisionObject* mCasterColObj;
        const btCollisionObject* mHitTarget;
        btVector3 mHitPosition;
        btVector3 mHitNormal;

        std::vector<const btCollisionObject*> mValidTargets;

        mutable std::mutex mMutex;

        PhysicsSystem* mPhysics;
        PhysicsTaskScheduler* mTaskScheduler;

        Projectile(const Projectile&);
        Projectile& operator=(const Projectile&);
    };

}

#endif
