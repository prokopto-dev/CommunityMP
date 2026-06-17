#ifndef OPENMW_MWPHYSICS_IPROJECTILE_H
#define OPENMW_MWPHYSICS_IPROJECTILE_H

#include <vector>

#include <osg/Vec3f>

#include "../mwworld/ptr.hpp"

namespace MWPhysics
{
    class IProjectile
    {
    public:
        virtual ~IProjectile() = default;

        virtual bool isActive() const = 0;
        virtual MWWorld::Ptr getTarget() const = 0;
        virtual MWWorld::Ptr getCaster() const = 0;
        virtual void setCaster(const MWWorld::Ptr& caster) = 0;
        virtual void setValidTargets(const std::vector<MWWorld::Ptr>& targets) = 0;

        virtual void setVelocity(osg::Vec3f velocity) = 0;
        virtual osg::Vec3f getSimulationPosition() const = 0;
        virtual osg::Vec3f getHitPosition() const = 0;
        virtual bool getHitWater() const = 0;
    };
}

#endif
