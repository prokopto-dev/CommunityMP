#ifndef OPENMW_MWPHYSICS_JOLTPHYSICSSYSTEM_H
#define OPENMW_MWPHYSICS_JOLTPHYSICSSYSTEM_H

#include "physicsbackend.hpp"

#if OPENMW_PHYSICS_USES_JOLT

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <map>
#include <memory>
#include <unordered_map>
#include <utility>

#include <osg/Group>
#include <osg/ref_ptr>

#include "../mwworld/livecellref.hpp"

#include "iphysicsbackend.hpp"

namespace Resource
{
    class ResourceSystem;
    class BulletShapeManager;
    class BulletShapeInstance;
}

namespace MWPhysics
{
    class JoltActor;
}

namespace MWPhysics
{
    // Jolt object layers — coarse grouping that mirrors OpenMW's
    // CollisionType well enough for broadphase. Static world chunks
    // (terrain, walls, closed doors) live in NON_MOVING; everything
    // that needs an integrator (actors, projectiles, animated doors)
    // lives in MOVING. Phase 6 may split further if the broadphase
    // tree shape proves unbalanced.
    namespace JoltLayers
    {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
        static constexpr JPH::uint NUM_LAYERS = 2;
    }

    namespace JoltBroadPhaseLayers
    {
        static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr JPH::uint NUM_LAYERS = 2;
    }

    // Implements ObjectLayer <-> BroadPhaseLayer mapping for Jolt's
    // broadphase. Single instance owned by JoltPhysicsSystem.
    class JoltBPLayerInterface final : public JPH::BroadPhaseLayerInterface
    {
    public:
        JoltBPLayerInterface();
        JPH::uint GetNumBroadPhaseLayers() const override { return JoltBroadPhaseLayers::NUM_LAYERS; }
        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override;
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override;
#endif

    private:
        JPH::BroadPhaseLayer mObjectToBroadPhase[JoltLayers::NUM_LAYERS];
    };

    class JoltObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override;
    };

    class JoltObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override;
    };

    // Minimal contact listener — just reports collisions to OpenMW's
    // collision callbacks. Phase 7 grows this for the actor controller.
    class JoltContactListener final : public JPH::ContactListener
    {
    public:
        JPH::ValidateResult OnContactValidate(const JPH::Body&, const JPH::Body&, JPH::RVec3Arg,
            const JPH::CollideShapeResult&) override
        {
            return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
        }
        void OnContactAdded(const JPH::Body&, const JPH::Body&, const JPH::ContactManifold&,
            JPH::ContactSettings&) override {}
        void OnContactPersisted(const JPH::Body&, const JPH::Body&, const JPH::ContactManifold&,
            JPH::ContactSettings&) override {}
        void OnContactRemoved(const JPH::SubShapeIDPair&) override {}
    };

    // Skeleton implementation of IPhysicsBackend backed by Jolt
    // Physics. Phase 4 of the migration: every method here throws
    // std::logic_error so the dispatch path can be exercised before
    // any real Jolt code lands. The constructor stores the
    // ResourceSystem and parent node but does not yet allocate a
    // JPH::PhysicsSystem — that's phase 5 (jolt-migration-plan.md).
    //
    // Compiled only when OPENMW_PHYSICS_BACKEND=jolt; under bullet
    // the corresponding .cpp collapses to an empty translation unit.
    class JoltPhysicsSystem final : public IPhysicsBackend
    {
    public:
        JoltPhysicsSystem(Resource::ResourceSystem* resourceSystem, osg::ref_ptr<osg::Group> parentNode);
        ~JoltPhysicsSystem() override;

        std::string_view name() const override { return physicsBackendName(); }

        // Every IPhysicsBackend method declared override; impls
        // are stubs in joltphysicssystem.cpp. The set is kept
        // alphabetised for diff hygiene.
        void enableWater(float height) override;
        void setWaterHeight(float height) override;
        void disableWater() override;

        void addObject(const MWWorld::Ptr& ptr, VFS::Path::NormalizedView mesh, osg::Quat rotation,
            int collisionType) override;
        void addActor(const MWWorld::Ptr& ptr, VFS::Path::NormalizedView mesh) override;
        int addProjectile(const MWWorld::Ptr& caster, const osg::Vec3f& position,
            VFS::Path::NormalizedView mesh, bool computeRadius) override;
        void setCaster(int projectileId, const MWWorld::Ptr& caster) override;
        void removeProjectile(int projectileId) override;
        void remove(const MWWorld::Ptr& ptr) override;

        void updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& updated) override;
        void updateScale(const MWWorld::Ptr& ptr) override;
        void updateRotation(const MWWorld::Ptr& ptr, osg::Quat rotate) override;
        void updatePosition(const MWWorld::Ptr& ptr) override;

        void addHeightField(const float* heights, int x, int y, int size, int verts, float minH, float maxH,
            const osg::Object* holdObject) override;
        void removeHeightField(int x, int y) override;
        const HeightField* getHeightField(int x, int y) const override;

        void stepSimulation(
            float dt, bool skipSimulation, osg::Timer_t frameStart, unsigned int frameNumber, osg::Stats& stats) override;
        void moveActors() override;
        bool toggleCollisionMode() override;
        void debugDraw() override;

        // RayCastingInterface (transitively through IPhysicsBackend)
        RayCastingResult castRay(const osg::Vec3f& from, const osg::Vec3f& to,
            const std::vector<MWWorld::ConstPtr>& ignore, const std::vector<MWWorld::Ptr>& targets, int mask,
            int group) const override;
        RayCastingResult castSphere(const osg::Vec3f& from, const osg::Vec3f& to, float radius, int mask,
            int group) const override;
        bool getLineOfSight(const MWWorld::ConstPtr& actor1, const MWWorld::ConstPtr& actor2) const override;

        std::vector<MWWorld::Ptr> getCollisions(
            const MWWorld::ConstPtr& ptr, int collisionGroup, int collisionMask) const override;
        std::vector<ContactPoint> getCollisionsPoints(
            const MWWorld::ConstPtr& ptr, int collisionGroup, int collisionMask) const override;
        osg::Vec3f traceDown(const MWWorld::Ptr& ptr, const osg::Vec3f& position, float maxHeight) override;

        bool isOnGround(const MWWorld::Ptr& actor) override;
        bool isOnSolidGround(const MWWorld::Ptr& actor) const override;
        bool canMoveToWaterSurface(const MWWorld::ConstPtr& actor, float waterlevel) override;

        osg::Vec3f getHalfExtents(const MWWorld::ConstPtr& actor) const override;
        osg::Vec3f getOriginalHalfExtents(const MWWorld::ConstPtr& actor) const override;
        osg::Vec3f getRenderingHalfExtents(const MWWorld::ConstPtr& actor) const override;
        osg::Vec3f getCollisionObjectPosition(const MWWorld::ConstPtr& actor) const override;
        osg::BoundingBox getBoundingBox(const MWWorld::ConstPtr& object) const override;

        void queueObjectMovement(const MWWorld::Ptr& ptr, const osg::Vec3f& velocity) override;
        void clearQueuedMovement() override;

        bool isActorStandingOn(const MWWorld::Ptr& actor, const MWWorld::ConstPtr& object) const override;
        void getActorsStandingOn(const MWWorld::ConstPtr& object, std::vector<MWWorld::Ptr>& out) const override;
        void getActorsCollidingWith(const MWWorld::ConstPtr& object, std::vector<MWWorld::Ptr>& out) const override;
        bool isObjectCollidingWith(const MWWorld::ConstPtr& object, ScriptedCollisionType type) const override;

        void markAsNonSolid(const MWWorld::ConstPtr& ptr) override;
        void updateAnimatedCollisionShape(const MWWorld::Ptr& object) override;
        bool isAreaOccupiedByOtherActor(
            const MWWorld::LiveCellRefBase* actor, const osg::Vec3f& position, float radius) const override;

        void reportCollision(const osg::Vec3f& position, const osg::Vec3f& normal) override;

        bool toggleDebugRendering() override;
        void reportStats(unsigned int frameNumber, osg::Stats& stats) const override;

        Actor* getActor(const MWWorld::Ptr& ptr) override;
        const Actor* getActor(const MWWorld::ConstPtr& ptr) const override;
        const Object* getObject(const MWWorld::ConstPtr& ptr) const override;
        Projectile* getProjectile(int projectileId) const override;

        Resource::BulletShapeManager* getShapeManager() override;
        float getPhysicsDt() const override;
        std::vector<std::pair<const IPhysicsObject*, bool>> getAnimatedObjects() const override;

    private:
        Resource::ResourceSystem* mResourceSystem;
        osg::ref_ptr<osg::Group> mParentNode;
        std::unique_ptr<Resource::BulletShapeManager> mShapeManager;
        float mPhysicsDt;

        // Jolt subsystems (phase 5). Allocated in the constructor,
        // released in the destructor in reverse order. JPH::Factory
        // is a singleton — a static guard ensures it's stood up at
        // most once per process even if multiple JoltPhysicsSystem
        // instances are created.
        std::unique_ptr<JPH::TempAllocator> mTempAllocator;
        std::unique_ptr<JPH::JobSystem> mJobSystem;
        JoltBPLayerInterface mBroadPhaseLayerInterface;
        JoltObjectLayerPairFilter mObjectLayerPairFilter;
        JoltObjectVsBroadPhaseLayerFilter mObjectVsBroadPhaseLayerFilter;
        JoltContactListener mContactListener;
        std::unique_ptr<JPH::PhysicsSystem> mJoltSystem;

        // Body bookkeeping. Keyed the same way PhysicsSystem keys
        // mObjects (LiveCellRefBase*) and mHeightFields (cell x,y);
        // makes the two implementations behaviour-compatible at the
        // public API.
        std::unordered_map<const MWWorld::LiveCellRefBase*, JPH::BodyID> mObjectBodies;
        std::map<std::pair<int, int>, JPH::BodyID> mHeightFieldBodies;
        JPH::BodyID mWaterBody;

        // Per-actor character controllers (phase 7).
        std::unordered_map<const MWWorld::LiveCellRefBase*, std::unique_ptr<JoltActor>> mActors;

        // Per-frame velocity queue for actors. Cleared at the end of
        // each stepSimulation. Mirrors PhysicsSystem's behaviour:
        // queueObjectMovement overwrites prior entries; the queue
        // is "valid until the next stepSimulation".
        std::unordered_map<const MWWorld::LiveCellRefBase*, osg::Vec3f> mQueuedMovement;

        // Projectiles (phase 8). Same int-id identity scheme as
        // PhysicsSystem so mwworld/projectilemanager doesn't need
        // to learn a new key type.
        std::unordered_map<int, JPH::BodyID> mProjectileBodies;
        int mNextProjectileId = 0;

        // Reverse map: BodyID's full u32 identifier -> the
        // MWWorld::Ptr that owns it. Populated alongside every
        // body lifecycle event so ray-cast / sphere-cast results
        // can resolve the hit Ptr and the BodyFilter can honour
        // ignore lists. Empty Ptr for environmental bodies (water,
        // height fields) - those just won't match any ignore.
        std::unordered_map<JPH::uint32, MWWorld::Ptr> mBodyOwners;

        // Phase 10a: animated objects need their JPH::Shape rebuilt
        // when the underlying btCompoundShape's child transforms
        // change. We keep the BulletShapeInstance alive (osg::ref_ptr
        // bumps the cache reference) so updateAnimatedCollisionShape
        // can re-run the converter on demand. The bool tracks
        // whether the navmesh navigator should refresh on next
        // tick (mirrors PhysicsSystem's mAnimatedObjects semantics).
        // The public-API Object* return path stays empty under Jolt
        // until phase 10b promotes Object to an abstract base; the
        // navigator's "this changed" signal still works because the
        // Jolt body is updated in-place via SetShape.
        struct AnimatedObjectEntry
        {
            osg::ref_ptr<Resource::BulletShapeInstance> mShapeInstance;
            JPH::BodyID mBodyId;
            bool mChanged = false;
        };
        std::unordered_map<const MWWorld::LiveCellRefBase*, AnimatedObjectEntry> mAnimatedObjectEntries;
    };
}

#endif // OPENMW_PHYSICS_USES_JOLT

#endif
