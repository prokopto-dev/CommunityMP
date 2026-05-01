#include "joltphysicssystem.hpp"

#if OPENMW_PHYSICS_USES_JOLT

#include <atomic>
#include <stdexcept>
#include <thread>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/RegisterTypes.h>

#include <components/debug/debuglog.hpp>
#include <components/resource/bulletshapemanager.hpp>

namespace MWPhysics
{
    namespace
    {
        constexpr float kPhysicsDtDefault = 1.0f / 60.0f;

        // Jolt body / pair / contact pool sizes. Tuned for vanilla MW
        // load (Vivec foreign quarter shows ~1700 active bodies in
        // peak combat); 4x headroom keeps allocations zero-cost
        // mid-frame.
        constexpr JPH::uint kMaxBodies = 10240;
        constexpr JPH::uint kNumBodyMutexes = 0; // 0 = autodetect
        constexpr JPH::uint kMaxBodyPairs = 65536;
        constexpr JPH::uint kMaxContactConstraints = 10240;

        // Temp allocator size — 10 MB matches Jolt's HelloWorld
        // recommendation for this body count.
        constexpr size_t kTempAllocatorBytes = 10 * 1024 * 1024;

        // Jolt's Factory + RegisterTypes are global state, must be
        // initialised at most once per process. Guard with a flag.
        std::atomic<bool> sJoltGlobalInitialised{ false };

        void initJoltGlobalsOnce()
        {
            bool expected = false;
            if (sJoltGlobalInitialised.compare_exchange_strong(expected, true))
            {
                JPH::RegisterDefaultAllocator();
                JPH::Factory::sInstance = new JPH::Factory();
                JPH::RegisterTypes();
            }
        }

        [[noreturn]] void notImplemented(const char* method)
        {
            // Keep the message format stable — phase-12 regression
            // suite greps for "JoltPhysicsSystem:" prefixes when
            // running the migration health check.
            throw std::logic_error(std::string("JoltPhysicsSystem: ") + method + " not implemented yet");
        }
    }

    // ----- JoltBPLayerInterface ---------------------------------------
    JoltBPLayerInterface::JoltBPLayerInterface()
    {
        mObjectToBroadPhase[JoltLayers::NON_MOVING] = JoltBroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[JoltLayers::MOVING] = JoltBroadPhaseLayers::MOVING;
    }

    JPH::BroadPhaseLayer JoltBPLayerInterface::GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const
    {
        return mObjectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* JoltBPLayerInterface::GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const
    {
        switch (static_cast<JPH::BroadPhaseLayer::Type>(inLayer))
        {
            case static_cast<JPH::BroadPhaseLayer::Type>(JoltBroadPhaseLayers::NON_MOVING):
                return "NON_MOVING";
            case static_cast<JPH::BroadPhaseLayer::Type>(JoltBroadPhaseLayers::MOVING):
                return "MOVING";
        }
        return "INVALID";
    }
#endif

    bool JoltObjectLayerPairFilter::ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const
    {
        switch (inObject1)
        {
            case JoltLayers::NON_MOVING:
                return inObject2 == JoltLayers::MOVING;
            case JoltLayers::MOVING:
                return true;
        }
        return false;
    }

    bool JoltObjectVsBroadPhaseLayerFilter::ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const
    {
        switch (inLayer1)
        {
            case JoltLayers::NON_MOVING:
                return inLayer2 == JoltBroadPhaseLayers::MOVING;
            case JoltLayers::MOVING:
                return true;
        }
        return false;
    }

    // ----- JoltPhysicsSystem ------------------------------------------
    JoltPhysicsSystem::JoltPhysicsSystem(
        Resource::ResourceSystem* resourceSystem, osg::ref_ptr<osg::Group> parentNode)
        : mResourceSystem(resourceSystem)
        , mParentNode(std::move(parentNode))
        , mPhysicsDt(kPhysicsDtDefault)
    {
        initJoltGlobalsOnce();

        mTempAllocator = std::make_unique<JPH::TempAllocatorImpl>(kTempAllocatorBytes);

        const int numWorkerThreads
            = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
        mJobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numWorkerThreads);

        mJoltSystem = std::make_unique<JPH::PhysicsSystem>();
        mJoltSystem->Init(kMaxBodies, kNumBodyMutexes, kMaxBodyPairs, kMaxContactConstraints,
            mBroadPhaseLayerInterface, mObjectVsBroadPhaseLayerFilter, mObjectLayerPairFilter);
        mJoltSystem->SetContactListener(&mContactListener);

        // Morrowind world units are centimetres, gravity ~ 9.81 m/s²
        // = 981 cm/s². Z is up.
        mJoltSystem->SetGravity(JPH::Vec3(0.0f, 0.0f, -981.0f));

        Log(Debug::Info) << "JoltPhysicsSystem: initialised "
                         << "(maxBodies=" << kMaxBodies
                         << " threads=" << numWorkerThreads << ")";
    }

    JoltPhysicsSystem::~JoltPhysicsSystem()
    {
        // Reverse-order teardown: PhysicsSystem first (it holds
        // references to layer interfaces / contact listener), then
        // job system, then temp allocator. Factory + types stay
        // alive for the rest of the process — destroying them would
        // race with any other JoltPhysicsSystem currently being
        // constructed (we don't expect this today, but cheap safety).
        mJoltSystem.reset();
        mJobSystem.reset();
        mTempAllocator.reset();
    }

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

    void JoltPhysicsSystem::stepSimulation(
        float dt, bool skipSimulation, osg::Timer_t /*frameStart*/, unsigned int /*frameNumber*/, osg::Stats& /*stats*/)
    {
        if (skipSimulation || dt <= 0.0f)
            return;

        // Phase 5: empty-world tick. Phase 7 will route the queued
        // movement and actor controllers through here. The single-
        // collision-step + single sub-step config is what
        // Jolt::HelloWorld uses; it's a fine starting point until we
        // bench against Bullet under the phase-12 rig.
        constexpr int collisionSteps = 1;
        mJoltSystem->Update(dt, collisionSteps, mTempAllocator.get(), mJobSystem.get());
    }
    // moveActors: phase 5 has no actors yet, so apply-positions is
    // trivially a no-op. Phase 7 implements actor controllers.
    void JoltPhysicsSystem::moveActors() {}
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
    // No-op until phase 7 wires actor controllers; lets engine startup
    // calls clear an (empty) queue without crashing.
    void JoltPhysicsSystem::clearQueuedMovement() {}

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
