#include "joltphysicssystem.hpp"

#if OPENMW_PHYSICS_USES_JOLT

#include <atomic>
#include <stdexcept>
#include <thread>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/RegisterTypes.h>

#include <components/debug/debuglog.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/bulletshape.hpp>
#include <components/resource/bulletshapemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vfs/manager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/class.hpp"

#include "joltactor.hpp"
#include "joltshapeconverter.hpp"

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
        // Tear down active bodies before dropping the PhysicsSystem
        // — Jolt asserts on destruction if any bodies are still
        // tracked. Order: water, objects, height fields. (The maps
        // would otherwise destruct in member-decl order which is
        // also fine, but explicit drains read better.)
        if (mJoltSystem)
        {
            auto& bi = mJoltSystem->GetBodyInterface();
            if (!mWaterBody.IsInvalid())
            {
                bi.RemoveBody(mWaterBody);
                bi.DestroyBody(mWaterBody);
            }
            for (auto& [_, id] : mObjectBodies)
            {
                bi.RemoveBody(id);
                bi.DestroyBody(id);
            }
            for (auto& [_, id] : mHeightFieldBodies)
            {
                bi.RemoveBody(id);
                bi.DestroyBody(id);
            }
            for (auto& [_, id] : mProjectileBodies)
            {
                bi.RemoveBody(id);
                bi.DestroyBody(id);
            }
        }
        mObjectBodies.clear();
        mHeightFieldBodies.clear();
        mProjectileBodies.clear();
        mActors.clear(); // CharacterVirtual destructors run before
                         // mJoltSystem so they can deregister cleanly.

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

    void JoltPhysicsSystem::enableWater(float height)
    {
        if (!mWaterBody.IsInvalid())
            disableWater();

        // Water is a thin sensor body — actors don't collide with it,
        // but ray queries can detect it for swim/buoyancy logic. A 1m
        // thick box covering ±1e6 in xy is a generous approximation
        // for an entire region's water plane.
        constexpr float kHalfThickness = 50.0f;     // ~1 m total
        constexpr float kHalfWidth = 1.0e6f;        // covers any cell
        const auto shape = JPH::RefConst<JPH::Shape>(new JPH::BoxShape(
            JPH::Vec3(kHalfWidth, kHalfWidth, kHalfThickness)));

        JPH::BodyCreationSettings settings(shape,
            JPH::RVec3(0.0f, 0.0f, height - kHalfThickness),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Static,
            JoltLayers::NON_MOVING);
        settings.mIsSensor = true;
        mWaterBody = mJoltSystem->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    }

    void JoltPhysicsSystem::setWaterHeight(float height)
    {
        if (mWaterBody.IsInvalid())
        {
            enableWater(height);
            return;
        }
        // Move the existing sensor up/down to track the new level.
        constexpr float kHalfThickness = 50.0f;
        mJoltSystem->GetBodyInterface().SetPosition(
            mWaterBody, JPH::RVec3(0.0f, 0.0f, height - kHalfThickness),
            JPH::EActivation::DontActivate);
    }

    void JoltPhysicsSystem::disableWater()
    {
        if (mWaterBody.IsInvalid())
            return;
        auto& bi = mJoltSystem->GetBodyInterface();
        bi.RemoveBody(mWaterBody);
        bi.DestroyBody(mWaterBody);
        mWaterBody = JPH::BodyID();
    }

    void JoltPhysicsSystem::addObject(
        const MWWorld::Ptr& ptr, VFS::Path::NormalizedView mesh, osg::Quat rotation, int /*collisionType*/)
    {
        // Skip postponed (e.g. world-load deferred) entries; matches
        // PhysicsSystem's gate.
        if (ptr.mRef->mData.mPhysicsPostponed)
            return;

        const VFS::Path::Normalized animationMesh = ptr.getClass().useAnim()
            ? Misc::ResourceHelpers::correctActorModelPath(mesh, mResourceSystem->getVFS())
            : VFS::Path::Normalized(mesh);
        osg::ref_ptr<Resource::BulletShapeInstance> shapeInstance
            = mShapeManager->getInstance(animationMesh);
        if (!shapeInstance || !shapeInstance->mCollisionShape)
            return;

        JPH::RefConst<JPH::Shape> joltShape = convertBulletShape(shapeInstance->mCollisionShape.get());
        if (!joltShape)
            return; // converter logged the reason

        // World position comes from the Ptr's cell ref; rotation is
        // the caller's. Object is static (terrain-anchored).
        const ESM::Position& pos = ptr.getRefData().getPosition();
        JPH::BodyCreationSettings settings(joltShape,
            JPH::RVec3(pos.pos[0], pos.pos[1], pos.pos[2]),
            JPH::Quat(rotation.x(), rotation.y(), rotation.z(), rotation.w()),
            JPH::EMotionType::Static,
            JoltLayers::NON_MOVING);
        const JPH::BodyID id
            = mJoltSystem->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        mObjectBodies.emplace(ptr.mRef, id);
        mBodyOwners.emplace(id.GetIndexAndSequenceNumber(), ptr);
    }
    void JoltPhysicsSystem::addActor(const MWWorld::Ptr& ptr, VFS::Path::NormalizedView mesh)
    {
        const VFS::Path::Normalized animationMesh
            = Misc::ResourceHelpers::correctActorModelPath(mesh, mResourceSystem->getVFS());
        osg::ref_ptr<const Resource::BulletShape> shape = mShapeManager->getShape(animationMesh);
        if (!shape && animationMesh != mesh)
            shape = mShapeManager->getShape(mesh);
        if (!shape)
            return;

        // Half-extents from the BulletShape's collision box.
        const osg::Vec3f halfExtents = shape->mCollisionBox.mExtents * 0.5f;
        if (halfExtents.length2() < 1e-6f)
            return; // shape has no usable bounds

        const ESM::Position& pos = ptr.getRefData().getPosition();
        const osg::Vec3f position(pos.pos[0], pos.pos[1], pos.pos[2]);

        auto actor = std::make_unique<JoltActor>(ptr, halfExtents, position, *mJoltSystem);
        mActors.emplace(ptr.mRef, std::move(actor));
    }
    int JoltPhysicsSystem::addProjectile(
        const MWWorld::Ptr& /*caster*/, const osg::Vec3f& position,
        VFS::Path::NormalizedView mesh, bool computeRadius)
    {
        float radius = 1.0f;
        if (computeRadius)
        {
            osg::ref_ptr<Resource::BulletShapeInstance> shapeInstance = mShapeManager->getInstance(mesh);
            if (shapeInstance)
                radius = shapeInstance->mCollisionBox.mExtents.length() * 0.5f;
        }

        // Sensor body — we want the projectile to detect collisions
        // (so the projectile manager can dispatch hit logic) without
        // physically pushing actors out of the way.
        const auto shape = JPH::RefConst<JPH::Shape>(new JPH::SphereShape(radius));
        JPH::BodyCreationSettings bcs(shape,
            JPH::RVec3(position.x(), position.y(), position.z()),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Kinematic,
            JoltLayers::MOVING);
        bcs.mIsSensor = true;
        const JPH::BodyID id
            = mJoltSystem->GetBodyInterface().CreateAndAddBody(bcs, JPH::EActivation::Activate);

        const int newId = ++mNextProjectileId;
        mProjectileBodies.emplace(newId, id);
        return newId;
    }

    void JoltPhysicsSystem::setCaster(int /*projectileId*/, const MWWorld::Ptr& /*caster*/)
    {
        // Phase 8b: the caster Ptr lives on the projectile so hit
        // resolution can attribute damage. UserData on the body is
        // the natural store; hooked up alongside the per-body Ptr
        // resolution work.
    }

    void JoltPhysicsSystem::removeProjectile(int projectileId)
    {
        const auto it = mProjectileBodies.find(projectileId);
        if (it == mProjectileBodies.end())
            return;
        auto& bi = mJoltSystem->GetBodyInterface();
        bi.RemoveBody(it->second);
        bi.DestroyBody(it->second);
        mProjectileBodies.erase(it);
    }
    void JoltPhysicsSystem::remove(const MWWorld::Ptr& ptr)
    {
        if (auto it = mObjectBodies.find(ptr.mRef); it != mObjectBodies.end())
        {
            auto& bi = mJoltSystem->GetBodyInterface();
            mBodyOwners.erase(it->second.GetIndexAndSequenceNumber());
            bi.RemoveBody(it->second);
            bi.DestroyBody(it->second);
            mObjectBodies.erase(it);
        }
        else if (auto ait = mActors.find(ptr.mRef); ait != mActors.end())
        {
            // JoltActor's destructor releases the CharacterVirtual;
            // the inner body owned by the character is collected
            // through that destruction path, no extra DestroyBody
            // call needed here.
            mActors.erase(ait);
        }
    }

    void JoltPhysicsSystem::updatePtr(const MWWorld::Ptr&, const MWWorld::Ptr&) { notImplemented("updatePtr"); }
    void JoltPhysicsSystem::updateScale(const MWWorld::Ptr&) { notImplemented("updateScale"); }
    void JoltPhysicsSystem::updateRotation(const MWWorld::Ptr&, osg::Quat) { notImplemented("updateRotation"); }
    void JoltPhysicsSystem::updatePosition(const MWWorld::Ptr&) { notImplemented("updatePosition"); }

    void JoltPhysicsSystem::addHeightField(
        const float* heights, int x, int y, int size, int verts, float minH, float maxH,
        const osg::Object* /*holdObject*/)
    {
        // Bullet's heightfield: heightStickWidth × heightStickLength
        // grid, height samples are in the local frame's third axis
        // (Z, upAxis=2). Cell footprint is `size` units, sampled at
        // `verts × verts` grid points.
        //
        // Jolt's HeightFieldShape: samples form an XZ grid with
        // heights along local +Y. Sample count must be a power of 2.
        // To match MW's Z-up convention we wrap the shape in a
        // RotatedTranslated that maps Jolt local Y -> world Z.
        //
        // Jolt requires sample count to be a power of 2; MW's
        // (verts-1) is a power of 2 by construction (vanilla 64+1 =
        // 65 sample points = 64-cell grid). HeightFieldShape's
        // sample count parameter is the side length, so we pass
        // verts directly and trust the upstream guarantee.
        const float scaling = static_cast<float>(size) / static_cast<float>(verts - 1);

        JPH::HeightFieldShapeSettings settings(heights,
            JPH::Vec3(0.0f, 0.0f, 0.0f),
            JPH::Vec3(scaling, 1.0f, scaling),
            static_cast<JPH::uint32>(verts));
        const auto baseResult = settings.Create();
        if (baseResult.HasError())
        {
            Log(Debug::Warning) << "Jolt HeightFieldShape rejected: " << baseResult.GetError();
            return;
        }

        // Rotate so the Jolt-local Y axis (height) becomes world Z.
        // 90° around X axis: (x, y, z) -> (x, -z, y).
        const JPH::Quat yToZ = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI);
        JPH::RotatedTranslatedShapeSettings rotSettings(JPH::Vec3::sZero(), yToZ, baseResult.Get().GetPtr());
        const auto rotResult = rotSettings.Create();
        if (rotResult.HasError())
        {
            Log(Debug::Warning) << "Jolt RotatedTranslated rejected: " << rotResult.GetError();
            return;
        }

        // Position: cell-corner offset matching the Bullet path
        // (BulletHelpers::getHeightfieldShift). MW cells are `size`
        // units square, anchored at (x*size, y*size).
        const float cx = static_cast<float>(x) * static_cast<float>(size);
        const float cy = static_cast<float>(y) * static_cast<float>(size);
        const float cz = 0.5f * (minH + maxH);

        JPH::BodyCreationSettings bcs(rotResult.Get(),
            JPH::RVec3(cx, cy, cz),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Static,
            JoltLayers::NON_MOVING);
        const JPH::BodyID id
            = mJoltSystem->GetBodyInterface().CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
        mHeightFieldBodies.emplace(std::make_pair(x, y), id);
    }

    void JoltPhysicsSystem::removeHeightField(int x, int y)
    {
        auto it = mHeightFieldBodies.find(std::make_pair(x, y));
        if (it == mHeightFieldBodies.end())
            return;
        auto& bi = mJoltSystem->GetBodyInterface();
        bi.RemoveBody(it->second);
        bi.DestroyBody(it->second);
        mHeightFieldBodies.erase(it);
    }

    const HeightField* JoltPhysicsSystem::getHeightField(int /*x*/, int /*y*/) const
    {
        // The MWPhysics::HeightField type is Bullet-specific (wraps
        // btHeightfieldTerrainShape). We don't construct one in the
        // Jolt path; callers either accept nullptr (the existing
        // PhysicsSystem path returns nullptr for unknown cells too)
        // or reach for the BodyID-based queries that phase 8 adds.
        return nullptr;
    }

    void JoltPhysicsSystem::stepSimulation(
        float dt, bool skipSimulation, osg::Timer_t /*frameStart*/, unsigned int /*frameNumber*/, osg::Stats& /*stats*/)
    {
        if (skipSimulation || dt <= 0.0f)
            return;

        // 1. Drain the per-actor velocity queue into each
        //    CharacterVirtual. Re-queuing the same actor between
        //    ticks would have already overwritten in the map.
        for (auto& [ref, actor] : mActors)
        {
            const auto qit = mQueuedMovement.find(ref);
            const osg::Vec3f vel = (qit != mQueuedMovement.end()) ? qit->second : osg::Vec3f();
            if (auto* cv = actor->getCharacter())
                cv->SetLinearVelocity(JPH::Vec3(vel.x(), vel.y(), vel.z()));
        }

        // 2. Tick the rigid-body world (objects, projectiles, water
        //    sensor). Single integration substep for now; phase 12
        //    benches whether MW's clock granularity wants more.
        constexpr int collisionSteps = 1;
        mJoltSystem->Update(dt, collisionSteps, mTempAllocator.get(), mJobSystem.get());

        // 3. Tick each character. ExtendedUpdate handles its own
        //    sub-stepping, slope sliding, stick-to-floor, and
        //    walk-stairs heuristics inside Jolt.
        const JPH::Vec3 gravity = mJoltSystem->GetGravity();
        const JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        const JPH::DefaultBroadPhaseLayerFilter bpFilter(mObjectVsBroadPhaseLayerFilter, JoltLayers::MOVING);
        const JPH::DefaultObjectLayerFilter objFilter(mObjectLayerPairFilter, JoltLayers::MOVING);
        const JPH::BodyFilter bodyFilter; // accept all (no per-body exclusions yet)
        const JPH::ShapeFilter shapeFilter; // accept all
        for (auto& [_, actor] : mActors)
        {
            if (auto* cv = actor->getCharacter())
            {
                cv->ExtendedUpdate(dt, gravity, updateSettings,
                    bpFilter, objFilter, bodyFilter, shapeFilter, *mTempAllocator);
            }
            actor->refreshState();
        }

        // 4. Movement queue is "valid until the next stepSimulation"
        //    per the API contract.
        mQueuedMovement.clear();
    }
    void JoltPhysicsSystem::moveActors()
    {
        auto world = MWBase::Environment::get().getWorld();
        for (auto& [_, actor] : mActors)
        {
            world->moveObject(actor->getPtr(), actor->getPosition(),
                /*movePhysics*/ false, /*moveToActive*/ false);
        }
    }
    bool JoltPhysicsSystem::toggleCollisionMode() { notImplemented("toggleCollisionMode"); }
    void JoltPhysicsSystem::debugDraw() { notImplemented("debugDraw"); }

    namespace
    {
        // BodyFilter that drops bodies whose owning Ptr appears in
        // the caller's ignore list. The owners map is captured by
        // reference so we don't pay copies per cast.
        class JoltIgnoreFilter final : public JPH::BodyFilter
        {
        public:
            JoltIgnoreFilter(const std::unordered_map<JPH::uint32, MWWorld::Ptr>& owners,
                const std::vector<MWWorld::ConstPtr>& ignore)
                : mOwners(owners)
                , mIgnore(ignore)
            {
            }
            bool ShouldCollide(const JPH::BodyID& bodyId) const override
            {
                if (mIgnore.empty())
                    return true;
                const auto it = mOwners.find(bodyId.GetIndexAndSequenceNumber());
                if (it == mOwners.end())
                    return true;
                for (const auto& p : mIgnore)
                    if (p.mRef == it->second.mRef)
                        return false;
                return true;
            }

        private:
            const std::unordered_map<JPH::uint32, MWWorld::Ptr>& mOwners;
            const std::vector<MWWorld::ConstPtr>& mIgnore;
        };
    }

    RayCastingResult JoltPhysicsSystem::castRay(
        const osg::Vec3f& from, const osg::Vec3f& to,
        const std::vector<MWWorld::ConstPtr>& ignore,
        const std::vector<MWWorld::Ptr>& /*targets*/, int /*mask*/, int /*group*/) const
    {
        // `targets` and `mask` are still TODO — targets is rarely
        // used (only for AI shoot-tests against specific actors),
        // and mask requires the full ObjectLayer expansion that
        // phase 5 deferred. Both will land alongside the per-actor
        // collision-group plumbing in phase 9.
        RayCastingResult result;
        result.mHit = false;

        const JPH::Vec3 dir(to.x() - from.x(), to.y() - from.y(), to.z() - from.z());
        const JPH::RRayCast ray(JPH::RVec3(from.x(), from.y(), from.z()), dir);

        JoltIgnoreFilter bodyFilter(mBodyOwners, ignore);
        JPH::RayCastResult hit;
        const bool didHit = mJoltSystem->GetNarrowPhaseQuery().CastRay(
            ray, hit, JPH::BroadPhaseLayerFilter(), JPH::ObjectLayerFilter(), bodyFilter);
        if (didHit)
        {
            result.mHit = true;
            const float f = hit.mFraction;
            const JPH::RVec3 hitPos = ray.GetPointOnRay(f);
            result.mHitPos = osg::Vec3f(hitPos.GetX(), hitPos.GetY(), hitPos.GetZ());

            JPH::BodyLockRead lock(mJoltSystem->GetBodyLockInterface(), hit.mBodyID);
            if (lock.Succeeded())
            {
                const JPH::Vec3 n = lock.GetBody().GetWorldSpaceSurfaceNormal(
                    hit.mSubShapeID2, hitPos);
                result.mHitNormal = osg::Vec3f(n.GetX(), n.GetY(), n.GetZ());
            }

            // Resolve the hit Ptr from the body owner map.
            const auto ownerIt = mBodyOwners.find(hit.mBodyID.GetIndexAndSequenceNumber());
            if (ownerIt != mBodyOwners.end())
                result.mHitObject = ownerIt->second;
        }
        return result;
    }

    RayCastingResult JoltPhysicsSystem::castSphere(
        const osg::Vec3f& from, const osg::Vec3f& to, float radius,
        int /*mask*/, int /*group*/) const
    {
        RayCastingResult result;
        result.mHit = false;

        const JPH::Vec3 direction(to.x() - from.x(), to.y() - from.y(), to.z() - from.z());
        const JPH::RShapeCast cast(new JPH::SphereShape(radius), JPH::Vec3::sReplicate(1.0f),
            JPH::RMat44::sTranslation(JPH::RVec3(from.x(), from.y(), from.z())), direction);

        // Closest-hit collector — first impact wins.
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        const JPH::ShapeCastSettings settings;
        mJoltSystem->GetNarrowPhaseQuery().CastShape(
            cast, settings, JPH::RVec3::sZero(), collector);

        if (collector.HadHit())
        {
            result.mHit = true;
            const float f = collector.mHit.mFraction;
            result.mHitPos = osg::Vec3f(
                from.x() + f * (to.x() - from.x()),
                from.y() + f * (to.y() - from.y()),
                from.z() + f * (to.z() - from.z()));
            // Cast normal: ShapeCast hits expose mPenetrationAxis
            // pointing into the swept shape; flip for the standard
            // outward-from-surface convention.
            const JPH::Vec3 n = -collector.mHit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sZero());
            result.mHitNormal = osg::Vec3f(n.GetX(), n.GetY(), n.GetZ());
        }
        return result;
    }

    bool JoltPhysicsSystem::getLineOfSight(const MWWorld::ConstPtr& a1, const MWWorld::ConstPtr& a2) const
    {
        // Cast a ray between the two actors' eye positions; LoS holds
        // if nothing's in the way. Eye height = top of the actor's
        // collision capsule.
        const auto getEye = [this](const MWWorld::ConstPtr& p) -> osg::Vec3f {
            const ESM::Position& pos = p.getRefData().getPosition();
            osg::Vec3f origin(pos.pos[0], pos.pos[1], pos.pos[2]);
            const auto it = mActors.find(p.mRef);
            if (it != mActors.end())
                origin.z() += it->second->getHalfExtents().z();
            return origin;
        };
        const RayCastingResult result = castRay(getEye(a1), getEye(a2), {}, {},
            CollisionType_World | CollisionType_HeightMap | CollisionType_Door, 0xff);
        return !result.mHit;
    }

    std::vector<MWWorld::Ptr> JoltPhysicsSystem::getCollisions(const MWWorld::ConstPtr&, int, int) const
    {
        notImplemented("getCollisions");
    }
    std::vector<ContactPoint> JoltPhysicsSystem::getCollisionsPoints(const MWWorld::ConstPtr&, int, int) const
    {
        notImplemented("getCollisionsPoints");
    }
    osg::Vec3f JoltPhysicsSystem::traceDown(
        const MWWorld::Ptr& /*ptr*/, const osg::Vec3f& position, float maxHeight)
    {
        // Drop a ray straight down. Returns where it hit ground, or
        // (position - maxHeight*Z) if the ray escapes to free space.
        const osg::Vec3f to(position.x(), position.y(), position.z() - maxHeight);
        const RayCastingResult hit = castRay(position, to, {}, {},
            CollisionType_World | CollisionType_HeightMap, 0xff);
        return hit.mHit ? hit.mHitPos : to;
    }

    bool JoltPhysicsSystem::isOnGround(const MWWorld::Ptr& ptr)
    {
        const auto it = mActors.find(ptr.mRef);
        return it != mActors.end() && it->second->isOnGround();
    }
    bool JoltPhysicsSystem::isOnSolidGround(const MWWorld::Ptr& ptr) const
    {
        const auto it = mActors.find(ptr.mRef);
        return it != mActors.end() && it->second->isOnGround();
    }
    bool JoltPhysicsSystem::canMoveToWaterSurface(const MWWorld::ConstPtr&, float)
    {
        notImplemented("canMoveToWaterSurface");
    }

    osg::Vec3f JoltPhysicsSystem::getHalfExtents(const MWWorld::ConstPtr& ptr) const
    {
        const auto it = mActors.find(ptr.mRef);
        return it != mActors.end() ? it->second->getHalfExtents() : osg::Vec3f();
    }
    osg::Vec3f JoltPhysicsSystem::getOriginalHalfExtents(const MWWorld::ConstPtr& ptr) const
    {
        // Bullet path differentiates the original (unscaled) and the
        // current (scaled) extents — actor scale is not yet wired
        // into the JoltActor (phase 7f), so the two values match.
        return getHalfExtents(ptr);
    }
    osg::Vec3f JoltPhysicsSystem::getRenderingHalfExtents(const MWWorld::ConstPtr& ptr) const
    {
        // Same situation as getOriginalHalfExtents — phase 7f tunes
        // a per-actor render scale to match the Bullet path's
        // mRenderingHalfExtents fudge factor.
        return getHalfExtents(ptr);
    }
    osg::Vec3f JoltPhysicsSystem::getCollisionObjectPosition(const MWWorld::ConstPtr& ptr) const
    {
        const auto it = mActors.find(ptr.mRef);
        return it != mActors.end() ? it->second->getPosition() : osg::Vec3f();
    }
    osg::BoundingBox JoltPhysicsSystem::getBoundingBox(const MWWorld::ConstPtr&) const
    {
        notImplemented("getBoundingBox");
    }

    void JoltPhysicsSystem::queueObjectMovement(const MWWorld::Ptr& ptr, const osg::Vec3f& velocity)
    {
        mQueuedMovement[ptr.mRef] = velocity;
    }
    void JoltPhysicsSystem::clearQueuedMovement() { mQueuedMovement.clear(); }

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
