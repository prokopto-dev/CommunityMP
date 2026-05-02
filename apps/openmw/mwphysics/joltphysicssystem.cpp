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
#include <components/misc/constants.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/bulletshape.hpp>
#include <components/resource/bulletshapemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/class.hpp"

#include "constants.hpp"
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
        , mShapeManager(std::make_unique<Resource::BulletShapeManager>(resourceSystem->getVFS(),
              resourceSystem->getSceneManager(), resourceSystem->getNifFileManager(),
              Settings::cells().mCacheExpiryDelay))
        , mPhysicsDt(kPhysicsDtDefault)
    {
        mResourceSystem->addResourceManager(mShapeManager.get());

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

        // OpenMW gravity matches Bullet's MovementSolver: 8.96 m/s² in
        // world-units coordinates, where 1 m = 69.99125 units. Earth
        // standard 981 cm/s² is wrong here — MW units are NOT plain
        // centimetres, and the engine GMSTs/animations are tuned to
        // this softer pull. Z is up.
        constexpr float kMwGravity = Constants::GravityConst * Constants::UnitsPerMeter;
        mJoltSystem->SetGravity(JPH::Vec3(0.0f, 0.0f, -kMwGravity));

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
        mObjectEntries.clear();
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

        if (mShapeManager)
            mResourceSystem->removeResourceManager(mShapeManager.get());
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

        // Always store an entry — getObject() returns it for the
        // navigator's recast feed (statics, doors, animated objects),
        // and updateAnimatedCollisionShape uses the shape-instance
        // pointer to rebuild the Jolt shape on animated objects only.
        JoltObjectEntry e;
        e.mShapeInstance = shapeInstance;
        e.mBodyId = id;
        e.mPtr = ptr;
        e.mLastPosition = JPH::RVec3(pos.pos[0], pos.pos[1], pos.pos[2]);
        e.mLastRotation = JPH::Quat(rotation.x(), rotation.y(), rotation.z(), rotation.w());
        mObjectEntries.emplace(ptr.mRef, std::move(e));
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
        // Phase 8d: register the CharacterVirtual's inner body in
        // the owner map so ray casts can resolve hits to this Ptr.
        if (auto* cv = actor->getCharacter())
        {
            const JPH::BodyID innerId = cv->GetInnerBodyID();
            if (!innerId.IsInvalid())
                mBodyOwners.emplace(innerId.GetIndexAndSequenceNumber(), ptr);
        }
        mActors.emplace(ptr.mRef, std::move(actor));
    }
    int JoltPhysicsSystem::addProjectile(
        const MWWorld::Ptr& caster, const osg::Vec3f& position,
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
        // Owner is the caster — castRay's ignore list filters out
        // the caster's own projectile so the shooter isn't auto-hit
        // by a self-fired arrow.
        mBodyOwners.emplace(id.GetIndexAndSequenceNumber(), caster);
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
        mBodyOwners.erase(it->second.GetIndexAndSequenceNumber());
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
            mObjectEntries.erase(ptr.mRef);
        }
        else if (auto ait = mActors.find(ptr.mRef); ait != mActors.end())
        {
            // Drop the owner-map entry before the JoltActor's
            // destructor releases the CharacterVirtual + inner body.
            if (auto* cv = ait->second->getCharacter())
            {
                const JPH::BodyID innerId = cv->GetInnerBodyID();
                if (!innerId.IsInvalid())
                    mBodyOwners.erase(innerId.GetIndexAndSequenceNumber());
            }
            mActors.erase(ait);
        }
    }

    void JoltPhysicsSystem::updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& updated)
    {
        // Cell change / save-load fixup. The internal maps key off
        // LiveCellRefBase* which is *stable* across cell transitions
        // (Ptr just gets a new CellStore), so the physics-side maps
        // don't need rekeying. Refresh the owner map's stored Ptr
        // so castRay returns the up-to-date Ptr (with current cell).
        if (auto it = mObjectBodies.find(old.mRef); it != mObjectBodies.end())
            mBodyOwners[it->second.GetIndexAndSequenceNumber()] = updated;
        if (auto it = mActors.find(old.mRef); it != mActors.end())
        {
            if (auto* cv = it->second->getCharacter())
            {
                const JPH::BodyID innerId = cv->GetInnerBodyID();
                if (!innerId.IsInvalid())
                    mBodyOwners[innerId.GetIndexAndSequenceNumber()] = updated;
            }
        }
    }

    void JoltPhysicsSystem::updateScale(const MWWorld::Ptr& /*ptr*/)
    {
        // Scaling a static body in Jolt requires either rebuilding
        // the shape (changing extents on a primitive) or wrapping
        // in a ScaledShape. Neither is wired up yet — vanilla MW
        // rarely scales static objects post-load, so a phase-10
        // task takes this on alongside the animated-collision-shape
        // refresh path.
    }

    void JoltPhysicsSystem::updateRotation(const MWWorld::Ptr& ptr, osg::Quat rotate)
    {
        const JPH::Quat jrot(rotate.x(), rotate.y(), rotate.z(), rotate.w());
        if (auto it = mObjectBodies.find(ptr.mRef); it != mObjectBodies.end())
        {
            mJoltSystem->GetBodyInterface().SetRotation(it->second, jrot,
                JPH::EActivation::DontActivate);
        }
        if (auto eit = mObjectEntries.find(ptr.mRef); eit != mObjectEntries.end())
        {
            eit->second.mLastRotation = jrot;
            eit->second.mChanged = true;
        }
        // Actor rotation is driven by gameplay code (turning); the
        // CharacterVirtual tracks its own rotation via SetRotation,
        // wired in phase 7f when the gameplay-physics handshake
        // lands.
    }

    void JoltPhysicsSystem::updatePosition(const MWWorld::Ptr& ptr)
    {
        const ESM::Position& pos = ptr.getRefData().getPosition();
        const JPH::RVec3 jpos(pos.pos[0], pos.pos[1], pos.pos[2]);
        if (auto it = mObjectBodies.find(ptr.mRef); it != mObjectBodies.end())
        {
            mJoltSystem->GetBodyInterface().SetPosition(it->second, jpos,
                JPH::EActivation::DontActivate);
        }
        else if (auto ait = mActors.find(ptr.mRef); ait != mActors.end())
        {
            if (auto* cv = ait->second->getCharacter())
                cv->SetPosition(jpos);
        }
        if (auto eit = mObjectEntries.find(ptr.mRef); eit != mObjectEntries.end())
        {
            eit->second.mLastPosition = jpos;
            eit->second.mChanged = true;
        }
    }

    void JoltPhysicsSystem::addHeightField(
        const float* heights, int x, int y, int size, int verts, float minH, float maxH,
        const osg::Object* /*holdObject*/)
    {
        // Bullet's heightfield: heightStickWidth × heightStickLength
        // grid, height samples are in the local frame's third axis
        // (Z, upAxis=2). Cell footprint is `size` units, sampled at
        // `verts × verts` grid points. OpenMW lays out heights
        // row-major with the row index increasing along world +Y.
        //
        // Jolt's HeightFieldShape: samples form an XZ grid (indexed
        // as samples[z*count+x]) with heights along local +Y. To
        // match MW's Z-up convention we wrap the shape in a 90°
        // rotation around X so local +Y becomes world +Z. That same
        // rotation also sends local +Z to world -Y, which flips the
        // grid's second axis. We compensate two ways:
        //   1. shift the local origin by (-size) along Z so the
        //      footprint sits at local Z ∈ [-size, 0]; after the
        //      rotation the cell ends up at world Y ∈ [0, size];
        //   2. reverse the row order of the heights array so the
        //      OpenMW row 0 (world Y=0) ends up at the trailing
        //      Jolt grid edge (post-rotation world Y=0).
        //
        // Jolt requires (sampleCount-1) to be a power of 2; MW's
        // (verts-1) is a power of 2 by construction (vanilla 64+1 =
        // 65 sample points = 64-cell grid).
        const float scaling = static_cast<float>(size) / static_cast<float>(verts - 1);

        std::vector<float> flippedHeights(static_cast<size_t>(verts) * verts);
        for (int r = 0; r < verts; ++r)
        {
            const int srcRow = (verts - 1 - r) * verts;
            const int dstRow = r * verts;
            std::copy_n(heights + srcRow, verts, flippedHeights.data() + dstRow);
        }

        JPH::HeightFieldShapeSettings settings(flippedHeights.data(),
            JPH::Vec3(0.0f, 0.0f, -static_cast<float>(size)),
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

        // Place the cell's bottom-left corner at (x*size, y*size).
        // After the rotation + offset, the heightfield's bottom-left
        // sample sits at local (0, 0, ?), so this BCS position is
        // also the world position of grid sample (0, 0).
        const float cx = static_cast<float>(x) * static_cast<float>(size);
        const float cy = static_cast<float>(y) * static_cast<float>(size);

        JPH::BodyCreationSettings bcs(rotResult.Get(),
            JPH::RVec3(cx, cy, 0.0f),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Static,
            JoltLayers::NON_MOVING);
        (void)minH; (void)maxH; // height range is implicit in the samples
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
        float dt, bool skipSimulation, osg::Timer_t /*frameStart*/, unsigned int frameNumber, osg::Stats& /*stats*/)
    {
        if (skipSimulation || dt <= 0.0f)
            return;

        // Diagnostic logging: set OPENMW_JOLT_TRACE=1 to dump per-actor
        // state every 30 frames. Phase 1 of docs/jolt-character-fix-plan.md.
        // Reads ground-state, velocity, and queued input AT BOTH ends of
        // the step so we can see whether ExtendedUpdate is the one
        // zeroing things out.
        static const bool sTrace = []() {
            const char* env = std::getenv("OPENMW_JOLT_TRACE");
            return env != nullptr && env[0] != '0' && env[0] != '\0';
        }();
        const bool traceThisFrame = sTrace && (frameNumber % 30 == 0);

        // 1. Drain the per-actor velocity queue into each
        //    CharacterVirtual.
        //
        // Phase 2 of docs/jolt-character-fix-plan.md: separate input
        // (horizontal walk velocity, jump impulse) from accumulated
        // vertical inertia. Mirrors MWPhysics::Actor::mInertia in the
        // Bullet path. Without this separation the per-frame input
        // overwrite was wiping the gravity-accumulated Z, so jumps
        // truncated, falls stalled, and standing was unstable.
        //
        // Inertia rules:
        //   - gameplay sends input.z() > 0 → jump: overwrite inertia.z
        //     with that value (one-shot impulse);
        //   - on ground (and not on slope): inertia.z reset to 0 so
        //     the actor sits flat instead of slowly drifting down;
        //   - on slope or airborne: inertia.z accumulates gravity * dt.
        const JPH::Vec3 stepGravity = mJoltSystem->GetGravity();
        for (auto& [ref, actor] : mActors)
        {
            auto* cv = actor->getCharacter();
            if (!cv)
                continue;
            const auto qit = mQueuedMovement.find(ref);
            const osg::Vec3f input = (qit != mQueuedMovement.end()) ? qit->second : osg::Vec3f();
            const auto groundState = cv->GetGroundState();
            const bool onGround = (groundState == JPH::CharacterVirtual::EGroundState::OnGround);
            const bool onSlope = (groundState == JPH::CharacterVirtual::EGroundState::OnSteepGround);

            float inertiaZ = actor->getInertiaZ();
            if (input.z() > 0.0f)
                inertiaZ = input.z(); // jump impulse
            else if (onGround && !onSlope)
                inertiaZ = 0.0f;       // floor absorbs the fall
            else
                inertiaZ += stepGravity.GetZ() * dt;
            actor->setInertiaZ(inertiaZ);

            // Total velocity = horizontal input + vertical inertia.
            // Negative input.z() (rare — scripted downward push) is
            // additive on top of inertia; positive was already
            // captured as the jump impulse above.
            const float zVel = inertiaZ + std::min(input.z(), 0.0f);
            cv->SetLinearVelocity(JPH::Vec3(input.x(), input.y(), zVel));

            if (traceThisFrame)
            {
                const auto p = cv->GetPosition();
                const auto curV = cv->GetLinearVelocity();
                Log(Debug::Info) << "[jolt-trace pre] f=" << frameNumber
                    << " " << actor->getPtr().getCellRef().getRefId().toDebugString()
                    << " pos=(" << p.GetX() << "," << p.GetY() << "," << p.GetZ() << ")"
                    << " in=(" << input.x() << "," << input.y() << "," << input.z() << ")"
                    << " inertiaZ=" << inertiaZ
                    << " setV=(" << input.x() << "," << input.y() << "," << zVel << ")"
                    << " ground=" << static_cast<int>(groundState)
                    << " curV=(" << curV.GetX() << "," << curV.GetY()
                        << "," << curV.GetZ() << ")"
                    << " dt=" << dt;
            }
        }

        // 2. Tick the rigid-body world (objects, projectiles, water
        //    sensor). Single integration substep for now; phase 12
        //    benches whether MW's clock granularity wants more.
        constexpr int collisionSteps = 1;
        mJoltSystem->Update(dt, collisionSteps, mTempAllocator.get(), mJobSystem.get());

        // 3. Tick each character. ExtendedUpdate handles its own
        //    sub-stepping, slope sliding, stick-to-floor, and
        //    walk-stairs heuristics inside Jolt.
        //
        // Jolt's ExtendedUpdateSettings defaults assume a Y-up world:
        //   mStickToFloorStepDown = (0, -0.5, 0)  // -Y "down"
        //   mWalkStairsStepUp     = (0,  0.4, 0)  // +Y "up"
        // MW is Z-up — without overriding these the stick-to-floor
        // raycast goes sideways instead of downward, ground state
        // flickers, and the step-up logic can't traverse vanilla
        // stairs. Use MW's stair sizes (sStepSizeUp = 34 units, ~48 cm)
        // so the heuristic matches what level designers expected.
        const JPH::Vec3 gravity = mJoltSystem->GetGravity();
        JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        updateSettings.mStickToFloorStepDown
            = JPH::Vec3(0.0f, 0.0f, -MWPhysics::sStepSizeDown);
        updateSettings.mWalkStairsStepUp
            = JPH::Vec3(0.0f, 0.0f, ::Constants::sStepSizeUp);
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

            if (traceThisFrame)
            {
                if (auto* cv = actor->getCharacter())
                {
                    const auto p = cv->GetPosition();
                    const auto v = cv->GetLinearVelocity();
                    Log(Debug::Info) << "[jolt-trace post] f=" << frameNumber
                        << " " << actor->getPtr().getCellRef().getRefId().toDebugString()
                        << " pos=(" << p.GetX() << "," << p.GetY() << "," << p.GetZ() << ")"
                        << " postV=(" << v.GetX() << "," << v.GetY() << "," << v.GetZ() << ")"
                        << " ground=" << static_cast<int>(cv->GetGroundState())
                        << " supported=" << (cv->IsSupported() ? 1 : 0);
                }
            }
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
    bool JoltPhysicsSystem::toggleCollisionMode()
    {
        // TCL-style noclip toggle. Vanilla MW just flips a flag on
        // the player actor; Jolt's CharacterVirtual gets the same
        // treatment via a Set<...>Layer call in phase 7f when the
        // player-actor handshake settles. Default-on for now.
        return true;
    }

    void JoltPhysicsSystem::debugDraw()
    {
        // No-op: phase 12 hooks JPH::DebugRenderer to OSG geometry
        // so the F4 collision-debug overlay works on the Jolt path.
    }

    namespace
    {
        // BodyFilter that
        //  - drops bodies whose owning Ptr appears in the caller's
        //    ignore list (the "don't hit yourself" path), and
        //  - if a targets list is provided, accepts ONLY bodies
        //    whose owning Ptr appears in that list (the AI shoot-
        //    test "is anyone of my known enemies in this line"
        //    path). Empty targets list = no restriction.
        // Both ignore/targets are captured by reference; the cast
        // call frame owns them long enough to outlive the filter.
        class JoltIgnoreFilter final : public JPH::BodyFilter
        {
        public:
            JoltIgnoreFilter(const std::unordered_map<JPH::uint32, MWWorld::Ptr>& owners,
                const std::vector<MWWorld::ConstPtr>& ignore,
                const std::vector<MWWorld::Ptr>& targets)
                : mOwners(owners)
                , mIgnore(ignore)
                , mTargets(targets)
            {
            }
            bool ShouldCollide(const JPH::BodyID& bodyId) const override
            {
                const auto it = mOwners.find(bodyId.GetIndexAndSequenceNumber());

                if (!mIgnore.empty() && it != mOwners.end())
                {
                    for (const auto& p : mIgnore)
                        if (p.mRef == it->second.mRef)
                            return false;
                }

                if (!mTargets.empty())
                {
                    if (it == mOwners.end())
                        return false; // no owner -> not in target list
                    for (const auto& p : mTargets)
                        if (p.mRef == it->second.mRef)
                            return true;
                    return false;
                }

                return true;
            }

        private:
            const std::unordered_map<JPH::uint32, MWWorld::Ptr>& mOwners;
            const std::vector<MWWorld::ConstPtr>& mIgnore;
            const std::vector<MWWorld::Ptr>& mTargets;
        };
    }

    RayCastingResult JoltPhysicsSystem::castRay(
        const osg::Vec3f& from, const osg::Vec3f& to,
        const std::vector<MWWorld::ConstPtr>& ignore,
        const std::vector<MWWorld::Ptr>& targets, int /*mask*/, int /*group*/) const
    {
        // `mask` (CollisionType bitmask) is still deferred — that
        // requires expanding ObjectLayers from the current 2-layer
        // (NON_MOVING / MOVING) split to one layer per CollisionType
        // bit, paired with a layer-pair filter table. Phase 6c′ when
        // there's a regression-test rig that catches the difference.
        RayCastingResult result;
        result.mHit = false;

        const JPH::Vec3 dir(to.x() - from.x(), to.y() - from.y(), to.z() - from.z());
        const JPH::RRayCast ray(JPH::RVec3(from.x(), from.y(), from.z()), dir);

        JoltIgnoreFilter bodyFilter(mBodyOwners, ignore, targets);
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

        // Closest-hit collector — first impact wins. Empty ignore
        // list (the public castSphere API doesn't currently take one;
        // this matches PhysicsSystem's behaviour).
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        const JPH::ShapeCastSettings settings;
        const std::vector<MWWorld::ConstPtr> emptyIgnore;
        const std::vector<MWWorld::Ptr> emptyTargets;
        const JoltIgnoreFilter bodyFilter(mBodyOwners, emptyIgnore, emptyTargets);
        mJoltSystem->GetNarrowPhaseQuery().CastShape(
            cast, settings, JPH::RVec3::sZero(), collector,
            JPH::BroadPhaseLayerFilter(), JPH::ObjectLayerFilter(), bodyFilter);

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

    std::vector<MWWorld::Ptr> JoltPhysicsSystem::getCollisions(
        const MWWorld::ConstPtr& /*ptr*/, int /*group*/, int /*mask*/) const
    {
        // Phase 8e: wire JPH::PhysicsSystem::GetActiveBodies +
        // CollideShape against the Ptr's body. For now return an
        // empty list — this is what the Bullet path returns when no
        // collisions are recorded for the frame, which is most of
        // the time anyway (only scripted callbacks consume it).
        return {};
    }

    std::vector<ContactPoint> JoltPhysicsSystem::getCollisionsPoints(
        const MWWorld::ConstPtr& /*ptr*/, int /*group*/, int /*mask*/) const
    {
        return {};
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
    bool JoltPhysicsSystem::canMoveToWaterSurface(
        const MWWorld::ConstPtr& /*actor*/, float /*waterlevel*/)
    {
        // Conservative true: vanilla actors can move toward water in
        // most situations. Phase 7f tightens this with a real
        // capsule-overlap query so AI doesn't try to swim through
        // landlocked water.
        return true;
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
    osg::BoundingBox JoltPhysicsSystem::getBoundingBox(const MWWorld::ConstPtr& ptr) const
    {
        // Approximate via half-extents centred on the actor /
        // object's current position. Good enough for AI camera
        // framing and spawn-overlap tests; exact body AABB is a
        // BodyLockRead away if any caller demands it.
        if (auto it = mActors.find(ptr.mRef); it != mActors.end())
        {
            const osg::Vec3f c = it->second->getPosition();
            const osg::Vec3f h = it->second->getHalfExtents();
            return osg::BoundingBox(c - h, c + h);
        }
        return osg::BoundingBox();
    }

    void JoltPhysicsSystem::queueObjectMovement(const MWWorld::Ptr& ptr, const osg::Vec3f& velocity)
    {
        mQueuedMovement[ptr.mRef] = velocity;
    }
    void JoltPhysicsSystem::clearQueuedMovement() { mQueuedMovement.clear(); }

    bool JoltPhysicsSystem::isActorStandingOn(
        const MWWorld::Ptr& /*actor*/, const MWWorld::ConstPtr& /*object*/) const
    {
        // Used by scripts that gate logic on "is the player on
        // <pressure plate>". JPH::CharacterVirtual::GetGroundBodyID
        // gives us the answer; phase 7f wires it (alongside the
        // step-up / slope-brake tuning that needs the same data).
        return false;
    }
    void JoltPhysicsSystem::getActorsStandingOn(
        const MWWorld::ConstPtr& /*object*/, std::vector<MWWorld::Ptr>& /*out*/) const
    {
        // Same data source as isActorStandingOn; phase 7f.
    }
    void JoltPhysicsSystem::getActorsCollidingWith(
        const MWWorld::ConstPtr& /*object*/, std::vector<MWWorld::Ptr>& /*out*/) const
    {
        // Filled by the contact listener in phase 7f.
    }
    bool JoltPhysicsSystem::isObjectCollidingWith(
        const MWWorld::ConstPtr& /*object*/, ScriptedCollisionType /*type*/) const
    {
        // Same: contact listener bookkeeping (phase 7f).
        return false;
    }

    void JoltPhysicsSystem::markAsNonSolid(const MWWorld::ConstPtr& /*ptr*/)
    {
        // Used to flag platforms / lifts so isOnSolidGround returns
        // false for actors riding them. Tracked via a side set on
        // PhysicsSystem; we'll mirror it in phase 7f when the
        // ground-contact data is available.
    }

    void JoltPhysicsSystem::updateAnimatedCollisionShape(const MWWorld::Ptr& object)
    {
        // Phase 10a: rebuild the runtime JPH::Shape from the
        // BulletShapeInstance's current state. The Bullet shape
        // itself is animated by mwworld's per-frame OSG -> Bullet
        // transform writeback; we read the up-to-date Bullet shape
        // here and feed it through the converter.
        //
        // Phase 10b will skip the rebuild when the Bullet shape's
        // child transforms haven't actually changed; for now we
        // unconditionally rebuild on call, matching the Bullet
        // path's "trust the caller's hint" behaviour.
        const auto it = mObjectEntries.find(object.mRef);
        if (it == mObjectEntries.end())
            return;
        JoltObjectEntry& entry = it->second;
        if (!entry.mShapeInstance || !entry.mShapeInstance->mCollisionShape)
            return;

        JPH::RefConst<JPH::Shape> joltShape
            = convertBulletShape(entry.mShapeInstance->mCollisionShape.get());
        if (!joltShape)
            return;

        mJoltSystem->GetBodyInterface().SetShape(entry.mBodyId, joltShape.GetPtr(),
            /*inUpdateMassProperties*/ false, JPH::EActivation::DontActivate);
        entry.mChanged = true;
    }

    bool JoltPhysicsSystem::isAreaOccupiedByOtherActor(
        const MWWorld::LiveCellRefBase* /*actor*/, const osg::Vec3f& /*position*/,
        float /*radius*/) const
    {
        // Used to keep AI from spawning into each other. Conservative
        // false: AI won't bunch up but the spawn safety check is
        // bypassed. JPH::PhysicsSystem::CollideShape with a sphere
        // gives the proper answer; wired in phase 12 once the
        // spawn-stability test rig from the regression suite exists.
        return false;
    }

    void JoltPhysicsSystem::reportCollision(const osg::Vec3f&, const osg::Vec3f&)
    {
        // No-op rather than throw: the only caller is mwworld door
        // collision telemetry; throwing would crash any door
        // interaction before the rest of the impl exists.
    }

    bool JoltPhysicsSystem::toggleDebugRendering() { return false; }
    void JoltPhysicsSystem::reportStats(unsigned int, osg::Stats&) const
    {
        // No-op: phase 12 will populate Jolt-specific stats.
    }

    Actor* JoltPhysicsSystem::getActor(const MWWorld::Ptr&) { return nullptr; }
    const Actor* JoltPhysicsSystem::getActor(const MWWorld::ConstPtr&) const { return nullptr; }
    const IPhysicsObject* JoltPhysicsSystem::getObject(const MWWorld::ConstPtr& ptr) const
    {
        const auto it = mObjectEntries.find(ptr.mRef);
        if (it == mObjectEntries.end())
            return nullptr;
        return &it->second;
    }
    Projectile* JoltPhysicsSystem::getProjectile(int) const { return nullptr; }

    Resource::BulletShapeManager* JoltPhysicsSystem::getShapeManager() { return mShapeManager.get(); }
    float JoltPhysicsSystem::getPhysicsDt() const { return mPhysicsDt; }
    btTransform JoltPhysicsSystem::JoltObjectEntry::getTransform() const
    {
        // Translate the cached Jolt pose into the navigator's
        // expected btTransform. Cached values are refreshed on each
        // updateAnimatedCollisionShape call.
        btTransform out;
        out.setOrigin(btVector3(mLastPosition.GetX(), mLastPosition.GetY(), mLastPosition.GetZ()));
        out.setRotation(btQuaternion(
            mLastRotation.GetX(), mLastRotation.GetY(), mLastRotation.GetZ(), mLastRotation.GetW()));
        return out;
    }

    std::vector<std::pair<const IPhysicsObject*, bool>> JoltPhysicsSystem::getAnimatedObjects() const
    {
        // Filter to truly animated entries — the navigator uses this
        // to decide whether to refresh navmesh tiles, so statics
        // (which never change shape) shouldn't pollute the list.
        std::vector<std::pair<const IPhysicsObject*, bool>> out;
        for (const auto& [_, entry] : mObjectEntries)
        {
            if (entry.mShapeInstance && entry.mShapeInstance->isAnimated())
                out.emplace_back(&entry, entry.mChanged);
        }
        return out;
    }
}

#endif // OPENMW_PHYSICS_USES_JOLT
