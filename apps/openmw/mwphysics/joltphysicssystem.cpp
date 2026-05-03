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
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
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
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwworld/cell.hpp"
#include "../mwworld/cellstore.hpp"
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

        // CharacterVirtual treats every collider returned by its
        // collide-shape query as a wall or floor, including bodies
        // flagged as sensors. The water plane is a sensor (so projectiles
        // / actors don't physically bounce off it), but without this
        // filter the CV walks across the water surface like it's
        // solid ground. Reject sensors at the per-body filter step so
        // they're invisible to the character's collision resolution
        // while still showing up to ray queries (swim detection uses
        // cell water height, not Jolt sensor contact, so this doesn't
        // affect isSwimming logic).
        //
        // The player's CV uses this base filter so it CAN see other
        // actors' inner bodies (ACTOR_PROBE) and physically push
        // against NPCs.
        class IgnoreSensorsBodyFilter final : public JPH::BodyFilter
        {
        public:
            bool ShouldCollideLocked(const JPH::Body& body) const override
            {
                return !body.IsSensor();
            }
        };

        // NPC variant: rejects sensors AND every actor probe. Without
        // the probe rejection, two NPCs spawned close to each other
        // mutually support each other on their inner bodies in
        // mid-air (the original symptom that motivated commit
        // dce3a2f012). With it, NPCs still can't push *through* each
        // other (their CVs don't intersect because each CV's shape
        // sweep finds its own probe via the standard CV self-filter
        // and the other CV's CV — which is virtual and not in the
        // broadphase). They just stop counting probes as "ground" or
        // "wall" support.
        class NpcBodyFilter final : public JPH::BodyFilter
        {
        public:
            bool ShouldCollideLocked(const JPH::Body& body) const override
            {
                if (body.IsSensor())
                    return false;
                if (body.GetObjectLayer() == JoltLayers::ACTOR_PROBE)
                    return false;
                return true;
            }
        };
    }

    // ----- JoltBPLayerInterface ---------------------------------------
    JoltBPLayerInterface::JoltBPLayerInterface()
    {
        mObjectToBroadPhase[JoltLayers::NON_MOVING] = JoltBroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[JoltLayers::MOVING] = JoltBroadPhaseLayers::MOVING;
        // Probes share the MOVING broadphase tree so projectile
        // raycasts (issued in the MOVING layer) find them without an
        // extra broadphase node. Per-pair gating happens in
        // JoltObjectLayerPairFilter below.
        mObjectToBroadPhase[JoltLayers::ACTOR_PROBE] = JoltBroadPhaseLayers::MOVING;
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
                return inObject2 == JoltLayers::MOVING || inObject2 == JoltLayers::ACTOR_PROBE;
            case JoltLayers::MOVING:
                return true;
            case JoltLayers::ACTOR_PROBE:
                // Probes are virtual presence markers for actors:
                // - vs MOVING: yes (CharacterVirtual / projectiles see them)
                // - vs NON_MOVING: yes (so probe ray-against-world still works)
                // - vs ACTOR_PROBE: NO (else inner bodies fight each other,
                //   the floating-NPC bug from commit dce3a2f012)
                return inObject2 != JoltLayers::ACTOR_PROBE;
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
            case JoltLayers::ACTOR_PROBE:
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
        // mExtents is already half-extents (computed as
        // (max - min) / 2 in BulletShapeManager); the Bullet path
        // also uses it directly via mOriginalHalfExtents. An earlier
        // version multiplied by 0.5 here, which quartered the
        // capsule and combined with the missing shape offset to
        // float actors ~0.5 m off the ground.
        const osg::Vec3f halfExtents = shape->mCollisionBox.mExtents;
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
    void JoltPhysicsSystem::promoteToDynamic(
        const MWWorld::Ptr& ptr, DynamicShape shape, const osg::Vec3f& halfExtents, float mass)
    {
        // Hold onto the source BulletShape before we drop the static
        // body — extractConvexHull walks it for the Mesh path. Once
        // mObjectEntries.erase runs, the osg::ref_ptr release could
        // free the shape if the manager's cache had no other holder.
        osg::ref_ptr<Resource::BulletShapeInstance> meshSource;
        if (auto eit = mObjectEntries.find(ptr.mRef); eit != mObjectEntries.end())
            meshSource = eit->second.mShapeInstance;

        // Tear down the existing static body if there is one — the
        // ImGui spawner places the ref via the normal addObject path
        // first so the rendering node exists, then upgrades it here.
        auto& bi = mJoltSystem->GetBodyInterface();
        if (auto it = mObjectBodies.find(ptr.mRef); it != mObjectBodies.end())
        {
            mBodyOwners.erase(it->second.GetIndexAndSequenceNumber());
            bi.RemoveBody(it->second);
            bi.DestroyBody(it->second);
            mObjectBodies.erase(it);
            mObjectEntries.erase(ptr.mRef);
        }

        // Auto-size the collider when we have the source mesh on
        // hand: NIF authors place the pivot at the foot, the mesh
        // box centre marks where the body's COM should sit relative
        // to that pivot, and the box extents give the actual size.
        // Without this, the user's UI defaults (32/32/48) usually
        // undershoot — a typical MW barrel is ~130 units tall, so
        // a 96-unit cylinder looks correct underneath but the visual
        // pokes out the top.
        JPH::Vec3 he = JPH::Vec3(std::max(halfExtents.x(), 8.0f),
            std::max(halfExtents.y(), 8.0f), std::max(halfExtents.z(), 8.0f));
        osg::Vec3f pivotLift(0.0f, 0.0f, he.GetZ());
        if (meshSource != nullptr)
        {
            const osg::Vec3f& meshExt = meshSource->mCollisionBox.mExtents;
            if (meshExt.length2() > 1.0f)
            {
                he = JPH::Vec3(std::max(meshExt.x(), 8.0f), std::max(meshExt.y(), 8.0f),
                    std::max(meshExt.z(), 8.0f));
                pivotLift = meshSource->mCollisionBox.mCenter;
            }
        }

        // Build the shape. Jolt's CylinderShape is aligned to its
        // local Y axis (top at +Y, bottom at -Y). MW is Z-up, so a
        // raw Cylinder lays on its side. Wrap in a RotatedTranslated
        // shape that maps Y → Z to stand it upright.
        JPH::RefConst<JPH::Shape> joltShape;
        switch (shape)
        {
            case DynamicShape::Box:
                joltShape = new JPH::BoxShape(he);
                break;
            case DynamicShape::Cylinder:
            {
                const float radius = 0.5f * (he.GetX() + he.GetY());
                JPH::RefConst<JPH::Shape> upright = new JPH::CylinderShape(he.GetZ(), radius);
                joltShape = new JPH::RotatedTranslatedShape(JPH::Vec3::sZero(),
                    JPH::Quat::sRotation(JPH::Vec3::sAxisX(), -0.5f * JPH::JPH_PI), upright.GetPtr());
                break;
            }
            case DynamicShape::Sphere:
            {
                const float radius = std::max({ he.GetX(), he.GetY(), he.GetZ() });
                joltShape = new JPH::SphereShape(radius);
                break;
            }
            case DynamicShape::Mesh:
            {
                // Convex hull preserves NIF vertex coords — pivot at
                // mesh foot baked in, no lift needed.
                if (meshSource && meshSource->mCollisionShape)
                    joltShape = extractConvexHull(meshSource->mCollisionShape.get());
                if (joltShape)
                {
                    pivotLift = osg::Vec3f(0.0f, 0.0f, 0.0f);
                }
                else
                {
                    const float radius = 0.5f * (he.GetX() + he.GetY());
                    JPH::RefConst<JPH::Shape> upright
                        = new JPH::CylinderShape(he.GetZ(), radius);
                    joltShape = new JPH::RotatedTranslatedShape(JPH::Vec3::sZero(),
                        JPH::Quat::sRotation(JPH::Vec3::sAxisX(), -0.5f * JPH::JPH_PI),
                        upright.GetPtr());
                }
                break;
            }
        }
        if (!joltShape)
            return;

        const ESM::Position& pos = ptr.getRefData().getPosition();
        // Convert MW Euler (XYZ around -X, -Y, -Z to match the rest
        // of the engine's rotation convention) to a Quat for Jolt.
        const osg::Quat q
            = osg::Quat(pos.rot[2], osg::Vec3f(0.0f, 0.0f, -1.0f))
            * osg::Quat(pos.rot[1], osg::Vec3f(0.0f, -1.0f, 0.0f))
            * osg::Quat(pos.rot[0], osg::Vec3f(-1.0f, 0.0f, 0.0f));

        // Spawn position = visual pivot + lift, rotated into world
        // by the initial body rotation. For zero rotation this is
        // just (pos + pivotLift); the rotation matters when an
        // object is placed already-tilted.
        const osg::Vec3f rotatedLift = q * pivotLift;
        JPH::BodyCreationSettings settings(joltShape,
            JPH::RVec3(pos.pos[0] + rotatedLift.x(), pos.pos[1] + rotatedLift.y(),
                pos.pos[2] + rotatedLift.z()),
            JPH::Quat(q.x(), q.y(), q.z(), q.w()), JPH::EMotionType::Dynamic, JoltLayers::MOVING);
        // Override mass; Jolt's volume-based default would be too
        // coarse for hand-tuned debug spawns. Inertia tensor is still
        // computed from the shape so rolling feels right.
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = mass;
        // Friction & restitution: pick numbers a barrel would have.
        settings.mFriction = 0.6f;
        settings.mRestitution = 0.2f;
        // Generous linear/angular damping so things settle and don't
        // ping around forever from the contact solver.
        settings.mLinearDamping = 0.1f;
        settings.mAngularDamping = 0.2f;

        const JPH::BodyID id = bi.CreateAndAddBody(settings, JPH::EActivation::Activate);
        mDynamicBodies.emplace(ptr.mRef, DynamicBody{ id, ptr, pivotLift });
        mBodyOwners.emplace(id.GetIndexAndSequenceNumber(), ptr);
    }

    void JoltPhysicsSystem::remove(const MWWorld::Ptr& ptr)
    {
        if (auto dit = mDynamicBodies.find(ptr.mRef); dit != mDynamicBodies.end())
        {
            auto& bi = mJoltSystem->GetBodyInterface();
            mBodyOwners.erase(dit->second.mBodyId.GetIndexAndSequenceNumber());
            bi.RemoveBody(dit->second.mBodyId);
            bi.DestroyBody(dit->second.mBodyId);
            mDynamicBodies.erase(dit);
            return;
        }
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

    void JoltPhysicsSystem::updateScale(const MWWorld::Ptr& ptr)
    {
        const auto bodyIt = mObjectBodies.find(ptr.mRef);
        if (bodyIt == mObjectBodies.end())
            return;

        // Wrap (or re-wrap) the body's shape in a ScaledShape so
        // the collision geometry tracks edits from the ImGui
        // inspector. Re-running the BulletShape→Jolt conversion
        // every edit would be expensive; instead we read the live
        // shape, peel a prior ScaledShape if there is one, and
        // attach a fresh one. Scale=1 still wraps — Jolt's
        // ScaledShape early-outs on identity scale internally.
        const float scale = ptr.getCellRef().getScale();
        JPH::BodyInterface& bi = mJoltSystem->GetBodyInterface();
        JPH::RefConst<JPH::Shape> current = bi.GetShape(bodyIt->second);
        JPH::RefConst<JPH::Shape> base = current;
        if (current != nullptr && current->GetSubType() == JPH::EShapeSubType::Scaled)
        {
            const auto* scaled = static_cast<const JPH::ScaledShape*>(current.GetPtr());
            base = scaled->GetInnerShape();
        }

        JPH::Ref<JPH::ScaledShape> scaled = new JPH::ScaledShape(base, JPH::Vec3(scale, scale, scale));
        bi.SetShape(bodyIt->second, scaled, /*updateMassProperties*/ false,
            JPH::EActivation::DontActivate);

        if (auto eit = mObjectEntries.find(ptr.mRef); eit != mObjectEntries.end())
            eit->second.mChanged = true;
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
        if (auto ait = mActors.find(ptr.mRef); ait != mActors.end())
        {
            ait->second->setRotation(rotate);
        }
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
            // Just set the position — JoltActor::updatePosition
            // handles inertia reset + ground-snap request flag. The
            // earlier in-place capsule sweep was causing exterior
            // cells to spuriously load right after an interior
            // teleport (cell preloader appears to re-evaluate when
            // position deltas are large) so the player ended up
            // visually rendered against the wrong region. The
            // post-teleport stick-to-floor in stepSimulation
            // (kPostTeleportSnap = 100 units) handles the actual
            // ground snap on the next physics tick.
            ait->second->updatePosition();
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
        auto& world = *MWBase::Environment::get().getWorld();
        for (auto& [ref, actor] : mActors)
        {
            auto* cv = actor->getCharacter();
            if (!cv)
                continue;
            // Inactive actors (cell freeze) skip the step entirely so
            // their position / inertia don't drift while the player
            // is across the world.
            if (!actor->isActive())
                continue;
            const auto qit = mQueuedMovement.find(ref);
            const osg::Vec3f input = (qit != mQueuedMovement.end()) ? qit->second : osg::Vec3f();
            const auto groundState = cv->GetGroundState();
            const bool onGround = (groundState == JPH::CharacterVirtual::EGroundState::OnGround);
            const bool onSlope = (groundState == JPH::CharacterVirtual::EGroundState::OnSteepGround);

            // Suspend gravity while swimming or flying — gameplay
            // computes a 3D desired velocity directly in those modes,
            // so leaving the inertia accumulator running would fight
            // the input. Mirrors MovementSolver's
            // `if (newPosition.z() < swimlevel || actor.mFlying)
            //     actor.mInertia = 0` branch.
            const MWWorld::Ptr ptr = actor->getPtr();
            const bool inWater = world.isSwimming(ptr);
            const bool flying = world.isFlying(ptr);
            const bool buoyant = inWater || flying;

            // Gameplay queues `input` in the ACTOR's local frame (the
            // animation system runs in local coords, see character.cpp
            // movementFromAnimation flow). Rotate to world space the
            // same way MovementSolver does:
            //   walk:       yaw only (rot[2] around -Z)
            //   swim/fly:   pitch + yaw (rot[0] around -X, rot[2] around -Z)
            const osg::Vec3f rot = ptr.getRefData().getPosition().asRotationVec3();
            osg::Vec3f worldInput;
            if (buoyant)
            {
                worldInput = (osg::Quat(rot.x(), osg::Vec3f(-1.0f, 0.0f, 0.0f))
                                 * osg::Quat(rot.z(), osg::Vec3f(0.0f, 0.0f, -1.0f)))
                    * input;
            }
            else
            {
                worldInput = osg::Quat(rot.z(), osg::Vec3f(0.0f, 0.0f, -1.0f)) * input;
            }

            float inertiaZ;
            if (buoyant)
            {
                inertiaZ = 0.0f;            // input drives Z directly below
            }
            else if (worldInput.z() > 0.0f)
            {
                inertiaZ = worldInput.z();  // jump impulse (world Z)
            }
            else if (onGround && !onSlope)
            {
                inertiaZ = 0.0f;            // floor absorbs the fall
            }
            else
            {
                inertiaZ = actor->getInertiaZ() + stepGravity.GetZ() * dt;
            }
            actor->setInertiaZ(inertiaZ);

            // Total velocity:
            //   - swimming/flying: pure rotated input (gameplay
            //     computed the full 3D desired velocity);
            //   - otherwise: horizontal worldInput + vertical inertia
            //     (negative worldInput.z = scripted downward push,
            //     additive on top of inertia).
            float zVel;
            if (buoyant)
                zVel = worldInput.z();
            else
                zVel = inertiaZ + std::min(worldInput.z(), 0.0f);
            cv->SetLinearVelocity(JPH::Vec3(worldInput.x(), worldInput.y(), zVel));

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

        // 2.5 Pull dynamic-body transforms back into RefData and the
        //     OSG node so the visual mesh tracks the physics body
        //     (Phase 6 of docs/imgui-overlay-plan.md). We bypass
        //     World::moveObject deliberately: that path would call
        //     mPhysics->updatePosition and overwrite the body we
        //     just read from. Setting the BaseNode directly is the
        //     same path Scene::updateObjectPosition takes.
        if (!mDynamicBodies.empty())
        {
            auto& bi = mJoltSystem->GetBodyInterfaceNoLock();
            const JPH::Vec3 gravity = mJoltSystem->GetGravity();
            for (auto& [_, dyn] : mDynamicBodies)
            {
                if (dyn.mPtr.isEmpty())
                    continue;
                // GetPosition returns the body's *local origin* in
                // world (= mPosition - rot * shape.GetCenterOfMass).
                // For Mesh hulls this already lands on the visual
                // pivot. For centred primitives we add a per-body
                // pivotLift offset that we subtract here, rotated by
                // the body so it tracks tipping.
                const JPH::RVec3 jpos = bi.GetPosition(dyn.mBodyId);
                const JPH::Quat jrot = bi.GetRotation(dyn.mBodyId);
                const osg::Quat osgRot(jrot.GetX(), jrot.GetY(), jrot.GetZ(), jrot.GetW());
                const osg::Vec3f rotatedLift = osgRot * dyn.mPivotLift;
                const osg::Vec3f visualPos(
                    jpos.GetX() - rotatedLift.x(), jpos.GetY() - rotatedLift.y(),
                    jpos.GetZ() - rotatedLift.z());

                // Buoyancy: if the body's centre is under the cell's
                // water level, push it back up via Jolt's native
                // helper. Buoyancy=1.5 ⇒ floats with the top third
                // poking above the surface; >1 floats, <1 sinks.
                // Linear/angular drag values come from Jolt's docs
                // for "object floating in water" (typical pond drag).
                if (auto* cell = dyn.mPtr.getCell();
                    cell != nullptr && cell->getCell() != nullptr && cell->getCell()->hasWater())
                {
                    const float waterZ = cell->getWaterLevel();
                    if (jpos.GetZ() < waterZ)
                    {
                        const JPH::RVec3 surfacePos(jpos.GetX(), jpos.GetY(), waterZ);
                        const JPH::Vec3 surfaceNormal(0.0f, 0.0f, 1.0f);
                        constexpr float kBuoyancy = 1.5f;
                        constexpr float kLinearDrag = 0.5f;
                        constexpr float kAngularDrag = 0.3f;
                        bi.ApplyBuoyancyImpulse(dyn.mBodyId, surfacePos, surfaceNormal, kBuoyancy,
                            kLinearDrag, kAngularDrag, JPH::Vec3::sZero(), gravity, dt);
                    }
                }

                // Persist position only — we keep the live rotation
                // exclusively on the OSG node (BaseNode::setAttitude
                // below). Decomposing Jolt's Quat back into MW's
                // intrinsic-ZYX Euler picks up gimbal-lock artefacts
                // for free-falling bodies; since the inspector / save
                // path use RefData.rot just for the *initial* orient
                // until we add a proper dynamic-state writer in
                // Phase 6c, leaving it stale is the lesser evil.
                ESM::Position pos = dyn.mPtr.getRefData().getPosition();
                pos.pos[0] = visualPos.x();
                pos.pos[1] = visualPos.y();
                pos.pos[2] = visualPos.z();
                dyn.mPtr.getRefData().setPosition(pos);

                if (auto* base = dyn.mPtr.getRefData().getBaseNode())
                {
                    base->setPosition(visualPos);
                    base->setAttitude(osgRot);
                }
            }
        }

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
        // Step-up cap = vanilla MW sStepSizeUp (34 MW units, ≈ 48 cm).
        // Going lower (we tried 21 = 0.3 m) blocked the heuristic on
        // taller risers MW NIFs ship with — Bethesda staircases mix
        // tread heights in the 25-40 cm range. 34 units accepts
        // every stair MW expects the player to climb.
        updateSettings.mWalkStairsStepUp
            = JPH::Vec3(0.0f, 0.0f, ::Constants::sStepSizeUp);
        // Walk-stairs forward-test geometry, scaled from Jolt's
        // metre-based defaults (0.02 / 0.15 m) to MW units (1 m ≈ 70).
        // Without this scaling the forward test fires for a smaller
        // distance than the per-frame velocity — the actor's capsule
        // just collides with the stair riser and stops, no step-up.
        // Min-step-forward is intentionally tiny (~1 mm) so even a
        // standing-walk speed engages the heuristic.
        // Step-forward-test bumped to 35 units (~half a metre, big
        // enough to overlap any reasonable stair tread depth) after
        // user reports the heuristic kept missing risers.
        updateSettings.mWalkStairsMinStepForward = 0.1f;
        updateSettings.mWalkStairsStepForwardTest = 35.0f;
        // Forward-contact angle threshold: -1 = "any angle qualifies".
        // Default cos(75°) and even our earlier cos(89°) rejected the
        // contacts MW stair risers actually generate (normals vary
        // slightly because of NIF tessellation). Take everything;
        // wrong-angle contacts get filtered later by the step-up
        // collision sweep itself.
        updateSettings.mWalkStairsCosAngleForwardContact = -1.0f;
        // Step-down extra: when the step-up + forward-test lands
        // somewhere, the CV needs to find ground below. Default is
        // zero, which fails on slightly recessed treads. Add 20
        // units (~30 cm) of extra search downward.
        updateSettings.mWalkStairsStepDownExtra
            = JPH::Vec3(0.0f, 0.0f, -20.0f);
        const JPH::DefaultBroadPhaseLayerFilter bpFilter(mObjectVsBroadPhaseLayerFilter, JoltLayers::MOVING);
        const JPH::DefaultObjectLayerFilter objFilter(mObjectLayerPairFilter, JoltLayers::MOVING);
        const IgnoreSensorsBodyFilter playerBodyFilter;
        const NpcBodyFilter npcBodyFilter;
        const JPH::ShapeFilter shapeFilter; // accept all
        const auto& playerRef = MWMechanics::getPlayer().mRef;
        // Post-teleport snap distance. Door spawn points and
        // place-at-mark scripts can land slightly above the floor
        // mesh — a generous stick-to-floor pulls the CV down onto
        // the surface instead of letting it free-fall the first
        // frame. Kept small (~1.4 m): too generous and the snap
        // teleports the player past intended landing spots
        // (e.g. balcony door entries snap to the ground floor).
        constexpr float kPostTeleportSnap = 100.0f;
        auto& worldRef = *MWBase::Environment::get().getWorld();
        for (auto& [_, actor] : mActors)
        {
            if (!actor->isActive())
                continue;
            // tcl no-clip: when collision mode is off, skip the
            // collision-resolving ExtendedUpdate entirely and let the
            // CharacterVirtual translate by mLinearVelocity * dt
            // straight through geometry. Mirrors PhysicsSystem's
            // toggleCollisionMode behaviour.
            auto* cv = actor->getCharacter();
            if (!cv)
                continue;
            // Snapshot z + ground state BEFORE ExtendedUpdate so we
            // can detect "started supported, ended in air" (begin
            // fall) and "started in air, ended supported" (land —
            // accumulate fall height + reset). Mirrors mtphysics.cpp:
            // 264-270 in the Bullet path.
            const float preZ = cv->GetPosition().GetZ();
            const auto preGround = cv->GetGroundState();
            const bool preSupported = (preGround == JPH::CharacterVirtual::EGroundState::OnGround);

            if (actor->getCollisionMode())
            {
                JPH::CharacterVirtual::ExtendedUpdateSettings perActor = updateSettings;
                JPH::Vec3 perActorGravity = gravity;
                // Buoyant (swim or fly): gameplay drives a full 3D
                // velocity directly. Letting Jolt apply gravity inside
                // ExtendedUpdate fights that input, and stick-to-floor
                // / walk-stairs heuristics try to snap the actor onto
                // the bed of the water (or the ground below a flying
                // actor). Disable both for buoyant actors.
                const MWWorld::Ptr ptr = actor->getPtr();
                const bool buoyant = worldRef.isSwimming(ptr) || worldRef.isFlying(ptr);
                if (buoyant)
                {
                    perActor.mStickToFloorStepDown = JPH::Vec3::sZero();
                    perActor.mWalkStairsStepUp = JPH::Vec3::sZero();
                    perActorGravity = JPH::Vec3::sZero();
                }
                else if (actor->consumeGroundSnapRequest())
                {
                    perActor.mStickToFloorStepDown
                        = JPH::Vec3(0.0f, 0.0f, -kPostTeleportSnap);
                }
                // Player CV needs to physically push against NPCs so
                // we feed it the inclusive filter (sensors out, probes
                // in). NPC CVs use the npc filter so they ignore
                // probes — fixes the floating-NPC bug without losing
                // arrows-hit-creature behaviour.
                const JPH::BodyFilter& bodyFilter = (ptr.mRef == playerRef)
                    ? static_cast<const JPH::BodyFilter&>(playerBodyFilter)
                    : static_cast<const JPH::BodyFilter&>(npcBodyFilter);
                cv->ExtendedUpdate(dt, perActorGravity, perActor,
                    bpFilter, objFilter, bodyFilter, shapeFilter, *mTempAllocator);
            }
            else
            {
                const JPH::Vec3 v = cv->GetLinearVelocity();
                const JPH::RVec3 p = cv->GetPosition();
                cv->SetPosition(JPH::RVec3(p.GetX() + v.GetX() * dt,
                    p.GetY() + v.GetY() * dt, p.GetZ() + v.GetZ() * dt));
            }
            actor->refreshState();

            // Fall-height tracking — mirrors mtphysics.cpp:264-270
            // in the Bullet path. addToFallHeight on each frame the
            // actor is in the air with negative z delta; stats.land()
            // when the actor reconnects with the ground (or is
            // swimming / flying / under slowfall — all paths that
            // cancel the accumulated fall).
            const float postZ = cv->GetPosition().GetZ();
            const auto postGround = cv->GetGroundState();
            const bool postSupported
                = (postGround == JPH::CharacterVirtual::EGroundState::OnGround);
            const float heightDiff = postZ - preZ;
            const MWWorld::Ptr ptr = actor->getPtr();
            if (ptr.getClass().isActor())
            {
                MWMechanics::CreatureStats& cstats = ptr.getClass().getCreatureStats(ptr);
                const bool buoyant = worldRef.isSwimming(ptr) || worldRef.isFlying(ptr);
                const bool stillOnGround = preSupported && postSupported;
                if (stillOnGround || buoyant)
                {
                    // land() with isOnPlatform = (player flying/swimming
                    // — same condition used in mtphysics.cpp:268).
                    cstats.land(ptr == MWMechanics::getPlayer() && buoyant);
                }
                else if (heightDiff < 0.0f)
                {
                    cstats.addToFallHeight(-heightDiff);
                }
            }

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

        // Phase-A water diagnostic: player only, every trace frame, dump
        // active contacts so we can see what's actually supporting the CV
        // (sensor body? cell static? heightfield?). The contact list is
        // populated by Jolt's NarrowPhaseQuery, filtered by our
        // IgnoreSensorsBodyFilter — if a sensor still appears here, the
        // filter is being bypassed somewhere we don't expect.
        if (traceThisFrame)
        {
            const auto& playerPtr = MWMechanics::getPlayer();
            const auto pit = mActors.find(playerPtr.mRef);
            if (pit != mActors.end())
            {
                if (auto* cv = pit->second->getCharacter())
                {
                    const auto& contacts = cv->GetActiveContacts();
                    const auto p = cv->GetPosition();
                    Log(Debug::Info) << "[jolt-water] f=" << frameNumber
                        << " pos.z=" << p.GetZ()
                        << " waterBodyValid=" << (mWaterBody.IsInvalid() ? 0 : 1)
                        << " isSwimming=" << (worldRef.isSwimming(playerPtr) ? 1 : 0)
                        << " isFlying=" << (worldRef.isFlying(playerPtr) ? 1 : 0)
                        << " contacts=" << contacts.size();
                    for (const auto& c : contacts)
                    {
                        Log(Debug::Info) << "[jolt-contact] body=" << c.mBodyB.GetIndexAndSequenceNumber()
                            << " sensor=" << (c.mIsSensorB ? 1 : 0)
                            << " hadColl=" << (c.mHadCollision ? 1 : 0)
                            << " discarded=" << (c.mWasDiscarded ? 1 : 0)
                            << " dist=" << c.mDistance
                            << " surfN=(" << c.mSurfaceNormal.GetX() << ","
                                          << c.mSurfaceNormal.GetY() << ","
                                          << c.mSurfaceNormal.GetZ() << ")"
                            << " isWaterBody=" << ((c.mBodyB == mWaterBody) ? 1 : 0);
                    }
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
        const auto& freshPlayerPtr = MWMechanics::getPlayer();
        for (auto& [_, actor] : mActors)
        {
            // For the player specifically, use the FRESHLY-fetched
            // Ptr (with its current cell pointer) instead of the
            // stale Ptr we stored when the JoltActor was constructed.
            // The stored mPtr keeps the cell from when the actor was
            // registered (typically the exterior the game starts in),
            // so after the player teleports to an interior, calling
            // world->moveObject(stalePtr, interiorPos) triggers the
            // cross-cell branch in 4-arg moveObject (currCell=oldExt,
            // newCell=computed-from-interior-coords-as-exterior) and
            // ends up calling Scene::changeToExteriorCell — which
            // unloads the interior we just teleported into and
            // teleports the player to (-162,-32,-22) interpreted as
            // exterior coords (= under sea level near origin).
            const MWWorld::Ptr& movePtr = (actor->getPtr().mRef == freshPlayerPtr.mRef)
                ? freshPlayerPtr
                : actor->getPtr();
            world->moveObject(movePtr, actor->getPosition(),
                /*movePhysics*/ false, /*moveToActive*/ false);
        }
    }
    bool JoltPhysicsSystem::toggleCollisionMode()
    {
        // TCL: flip the player actor's internal collision flag. The
        // step loop honours mInternalCollision by skipping
        // ExtendedUpdate (collision resolution) and translating the
        // CharacterVirtual freely instead.
        const auto& playerPtr = MWMechanics::getPlayer();
        auto it = mActors.find(playerPtr.mRef);
        if (it == mActors.end())
            return true;
        const bool newMode = !it->second->getCollisionMode();
        it->second->enableCollisionMode(newMode);
        return newMode;
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
        // Ignore both actors so the LoS ray doesn't trip on either
        // CharacterVirtual's inner body. With probes registered on
        // ACTOR_PROBE the player's own inner body sits at the eye
        // position the ray starts from — without the self-ignore the
        // ray hits its origin's own probe and getHitContact bails
        // out, making melee miss every NPC.
        const std::vector<MWWorld::ConstPtr> ignore{ a1, a2 };
        const RayCastingResult result = castRay(getEye(a1), getEye(a2), ignore, {},
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
        const MWWorld::Ptr& /*ptr*/, const osg::Vec3f& position, float /*maxHeight*/)
    {
        // No-op pass-through — returning the input means
        // World::adjustPosition does `min(pos.z+20, pos.z+20) =
        // pos.z+20`, the player ends up exactly at the door /
        // teleport spawn (plus the engine's 20-unit safety nudge),
        // and the next stepSimulation's post-teleport stick-to-floor
        // (mPostTeleportSnap = 100 in stepSimulation) snaps the CV
        // onto whatever surface is below.
        //
        // Earlier attempts replicated Bullet's ActorTracer::findGround
        // — first via a thin ray (slipped through hollow floor
        // boxes → "dessous la map"), then a sphere cast (off by the
        // sphere radius), then a capsule cast using the actor's
        // shape (still landed in surprising places, user reported
        // "vraiment pas à la bonne endroit"). The CV-driven snap on
        // the next physics step is closer to what the user described
        // wanting: pause physics conceptually, set the position,
        // refresh, resume — let the simulator decide where the
        // ground is once the actor's CV is there.
        return position;
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

    IPhysicsActor* JoltPhysicsSystem::getActor(const MWWorld::Ptr& ptr)
    {
        const auto it = mActors.find(ptr.mRef);
        return it != mActors.end() ? it->second.get() : nullptr;
    }
    const IPhysicsActor* JoltPhysicsSystem::getActor(const MWWorld::ConstPtr& ptr) const
    {
        const auto it = mActors.find(ptr.mRef);
        return it != mActors.end() ? it->second.get() : nullptr;
    }
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
