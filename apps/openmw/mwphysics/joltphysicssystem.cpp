#include "joltphysicssystem.hpp"

#if OPENMW_PHYSICS_USES_JOLT

#include <stdexcept>

#include <components/resource/bulletshapemanager.hpp>

namespace MWPhysics
{
    namespace
    {
        // Sentinel default for getPhysicsDt until phase 5 reads the
        // user setting (Settings::physics().mAsyncNumThreads etc.).
        constexpr float kPhysicsDtDefault = 1.0f / 60.0f;

        [[noreturn]] void notImplemented(const char* method)
        {
            // Keep the message format stable — phase-12 regression
            // suite greps for "JoltPhysicsSystem:" prefixes when
            // running the migration health check.
            throw std::logic_error(std::string("JoltPhysicsSystem: ") + method + " not implemented yet");
        }
    }

    JoltPhysicsSystem::JoltPhysicsSystem(
        Resource::ResourceSystem* resourceSystem, osg::ref_ptr<osg::Group> parentNode)
        : mResourceSystem(resourceSystem)
        , mParentNode(std::move(parentNode))
        , mPhysicsDt(kPhysicsDtDefault)
    {
        // Phase 5 will populate mShapeManager and stand up the
        // JPH::PhysicsSystem here. For now we just keep the
        // pointers alive so the engine can construct the backend
        // and learn that everything else throws.
    }

    JoltPhysicsSystem::~JoltPhysicsSystem() = default;

    // --- Stubs --------------------------------------------------------
    // Every method below is a one-liner that throws. The grouping
    // mirrors IPhysicsBackend exactly. When phase 5 lands, this file
    // is the single place that gets edited per-method.

    void JoltPhysicsSystem::enableWater(float) { notImplemented("enableWater"); }
    void JoltPhysicsSystem::setWaterHeight(float) { notImplemented("setWaterHeight"); }
    void JoltPhysicsSystem::disableWater() { notImplemented("disableWater"); }

    void JoltPhysicsSystem::addObject(const MWWorld::Ptr&, VFS::Path::NormalizedView, osg::Quat, int)
    {
        notImplemented("addObject");
    }
    void JoltPhysicsSystem::addActor(const MWWorld::Ptr&, VFS::Path::NormalizedView) { notImplemented("addActor"); }
    int JoltPhysicsSystem::addProjectile(const MWWorld::Ptr&, const osg::Vec3f&, VFS::Path::NormalizedView, bool)
    {
        notImplemented("addProjectile");
    }
    void JoltPhysicsSystem::setCaster(int, const MWWorld::Ptr&) { notImplemented("setCaster"); }
    void JoltPhysicsSystem::removeProjectile(int) { notImplemented("removeProjectile"); }
    void JoltPhysicsSystem::remove(const MWWorld::Ptr&) { notImplemented("remove"); }

    void JoltPhysicsSystem::updatePtr(const MWWorld::Ptr&, const MWWorld::Ptr&) { notImplemented("updatePtr"); }
    void JoltPhysicsSystem::updateScale(const MWWorld::Ptr&) { notImplemented("updateScale"); }
    void JoltPhysicsSystem::updateRotation(const MWWorld::Ptr&, osg::Quat) { notImplemented("updateRotation"); }
    void JoltPhysicsSystem::updatePosition(const MWWorld::Ptr&) { notImplemented("updatePosition"); }

    void JoltPhysicsSystem::addHeightField(
        const float*, int, int, int, int, float, float, const osg::Object*)
    {
        notImplemented("addHeightField");
    }
    void JoltPhysicsSystem::removeHeightField(int, int) { notImplemented("removeHeightField"); }
    const HeightField* JoltPhysicsSystem::getHeightField(int, int) const { notImplemented("getHeightField"); }

    void JoltPhysicsSystem::stepSimulation(float, bool, osg::Timer_t, unsigned int, osg::Stats&)
    {
        notImplemented("stepSimulation");
    }
    void JoltPhysicsSystem::moveActors() { notImplemented("moveActors"); }
    bool JoltPhysicsSystem::toggleCollisionMode() { notImplemented("toggleCollisionMode"); }
    void JoltPhysicsSystem::debugDraw() { notImplemented("debugDraw"); }

    RayCastingResult JoltPhysicsSystem::castRay(
        const osg::Vec3f&, const osg::Vec3f&, const std::vector<MWWorld::ConstPtr>&,
        const std::vector<MWWorld::Ptr>&, int, int) const
    {
        notImplemented("castRay");
    }
    RayCastingResult JoltPhysicsSystem::castSphere(
        const osg::Vec3f&, const osg::Vec3f&, float, int, int) const
    {
        notImplemented("castSphere");
    }
    bool JoltPhysicsSystem::getLineOfSight(const MWWorld::ConstPtr&, const MWWorld::ConstPtr&) const
    {
        notImplemented("getLineOfSight");
    }

    std::vector<MWWorld::Ptr> JoltPhysicsSystem::getCollisions(const MWWorld::ConstPtr&, int, int) const
    {
        notImplemented("getCollisions");
    }
    std::vector<ContactPoint> JoltPhysicsSystem::getCollisionsPoints(const MWWorld::ConstPtr&, int, int) const
    {
        notImplemented("getCollisionsPoints");
    }
    osg::Vec3f JoltPhysicsSystem::traceDown(const MWWorld::Ptr&, const osg::Vec3f&, float)
    {
        notImplemented("traceDown");
    }

    bool JoltPhysicsSystem::isOnGround(const MWWorld::Ptr&) { notImplemented("isOnGround"); }
    bool JoltPhysicsSystem::isOnSolidGround(const MWWorld::Ptr&) const { notImplemented("isOnSolidGround"); }
    bool JoltPhysicsSystem::canMoveToWaterSurface(const MWWorld::ConstPtr&, float)
    {
        notImplemented("canMoveToWaterSurface");
    }

    osg::Vec3f JoltPhysicsSystem::getHalfExtents(const MWWorld::ConstPtr&) const { notImplemented("getHalfExtents"); }
    osg::Vec3f JoltPhysicsSystem::getOriginalHalfExtents(const MWWorld::ConstPtr&) const
    {
        notImplemented("getOriginalHalfExtents");
    }
    osg::Vec3f JoltPhysicsSystem::getRenderingHalfExtents(const MWWorld::ConstPtr&) const
    {
        notImplemented("getRenderingHalfExtents");
    }
    osg::Vec3f JoltPhysicsSystem::getCollisionObjectPosition(const MWWorld::ConstPtr&) const
    {
        notImplemented("getCollisionObjectPosition");
    }
    osg::BoundingBox JoltPhysicsSystem::getBoundingBox(const MWWorld::ConstPtr&) const
    {
        notImplemented("getBoundingBox");
    }

    void JoltPhysicsSystem::queueObjectMovement(const MWWorld::Ptr&, const osg::Vec3f&)
    {
        notImplemented("queueObjectMovement");
    }
    void JoltPhysicsSystem::clearQueuedMovement() { notImplemented("clearQueuedMovement"); }

    bool JoltPhysicsSystem::isActorStandingOn(const MWWorld::Ptr&, const MWWorld::ConstPtr&) const
    {
        notImplemented("isActorStandingOn");
    }
    void JoltPhysicsSystem::getActorsStandingOn(const MWWorld::ConstPtr&, std::vector<MWWorld::Ptr>&) const
    {
        notImplemented("getActorsStandingOn");
    }
    void JoltPhysicsSystem::getActorsCollidingWith(const MWWorld::ConstPtr&, std::vector<MWWorld::Ptr>&) const
    {
        notImplemented("getActorsCollidingWith");
    }
    bool JoltPhysicsSystem::isObjectCollidingWith(const MWWorld::ConstPtr&, ScriptedCollisionType) const
    {
        notImplemented("isObjectCollidingWith");
    }

    void JoltPhysicsSystem::markAsNonSolid(const MWWorld::ConstPtr&) { notImplemented("markAsNonSolid"); }
    void JoltPhysicsSystem::updateAnimatedCollisionShape(const MWWorld::Ptr&)
    {
        notImplemented("updateAnimatedCollisionShape");
    }
    bool JoltPhysicsSystem::isAreaOccupiedByOtherActor(
        const MWWorld::LiveCellRefBase*, const osg::Vec3f&, float) const
    {
        notImplemented("isAreaOccupiedByOtherActor");
    }

    void JoltPhysicsSystem::reportCollision(const osg::Vec3f&, const osg::Vec3f&)
    {
        // No-op rather than throw: the only caller is mwworld door
        // collision telemetry; throwing would crash any door
        // interaction before the rest of the impl exists.
    }

    bool JoltPhysicsSystem::toggleDebugRendering() { notImplemented("toggleDebugRendering"); }
    void JoltPhysicsSystem::reportStats(unsigned int, osg::Stats&) const
    {
        // No-op: phase 12 will populate Jolt-specific stats.
    }

    Actor* JoltPhysicsSystem::getActor(const MWWorld::Ptr&) { return nullptr; }
    const Actor* JoltPhysicsSystem::getActor(const MWWorld::ConstPtr&) const { return nullptr; }
    const Object* JoltPhysicsSystem::getObject(const MWWorld::ConstPtr&) const { return nullptr; }
    Projectile* JoltPhysicsSystem::getProjectile(int) const { return nullptr; }

    Resource::BulletShapeManager* JoltPhysicsSystem::getShapeManager() { return mShapeManager.get(); }
    float JoltPhysicsSystem::getPhysicsDt() const { return mPhysicsDt; }
    std::vector<std::pair<const Object*, bool>> JoltPhysicsSystem::getAnimatedObjects() const { return {}; }
}

#endif // OPENMW_PHYSICS_USES_JOLT
