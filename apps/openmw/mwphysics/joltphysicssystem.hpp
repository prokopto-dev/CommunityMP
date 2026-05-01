#ifndef OPENMW_MWPHYSICS_JOLTPHYSICSSYSTEM_H
#define OPENMW_MWPHYSICS_JOLTPHYSICSSYSTEM_H

#include "physicsbackend.hpp"

#if OPENMW_PHYSICS_USES_JOLT

#include <memory>

#include <osg/ref_ptr>

#include "iphysicsbackend.hpp"

namespace osg
{
    class Group;
}

namespace Resource
{
    class ResourceSystem;
    class BulletShapeManager;
}

namespace MWPhysics
{
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
        std::vector<std::pair<const Object*, bool>> getAnimatedObjects() const override;

    private:
        Resource::ResourceSystem* mResourceSystem;
        osg::ref_ptr<osg::Group> mParentNode;
        std::unique_ptr<Resource::BulletShapeManager> mShapeManager;
        float mPhysicsDt;
    };
}

#endif // OPENMW_PHYSICS_USES_JOLT

#endif
