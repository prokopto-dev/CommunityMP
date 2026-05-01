#ifndef OPENMW_MWPHYSICS_PHYSICSSYSTEM_H
#define OPENMW_MWPHYSICS_PHYSICSSYSTEM_H

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <variant>

#include <osg/BoundingBox>
#include <osg/Quat>
#include <osg/Timer>
#include <osg/ref_ptr>

#include <components/vfs/pathutil.hpp>

#include "../mwworld/ptr.hpp"

#include "collisiontype.hpp"
#include "iphysicsbackend.hpp"
#include "physicsbackend.hpp"
#include "raycasting.hpp"

namespace osg
{
    class Group;
    class Object;
    class Stats;
}

namespace MWRender
{
    class DebugDrawer;
}

namespace Resource
{
    class BulletShapeManager;
    class ResourceSystem;
}

class btCollisionWorld;
class btBroadphaseInterface;
class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class btCollisionObject;
class btCollisionShape;
class btVector3;

namespace MWPhysics
{
    class HeightField;
    class Object;
    class Actor;
    class PhysicsTaskScheduler;
    class Projectile;
    enum ScriptedCollisionType : char;

    using ActorMap = std::unordered_map<const MWWorld::LiveCellRefBase*, std::shared_ptr<Actor>>;

    struct ContactPoint
    {
        MWWorld::Ptr mObject;
        osg::Vec3f mPoint;
        osg::Vec3f mNormal;
    };

    struct LOSRequest
    {
        LOSRequest(const std::weak_ptr<Actor>& a1, const std::weak_ptr<Actor>& a2);
        std::array<std::weak_ptr<Actor>, 2> mActors;
        std::array<const Actor*, 2> mRawActors;
        bool mResult;
        bool mStale;
        int mAge;
    };
    bool operator==(const LOSRequest& lhs, const LOSRequest& rhs) noexcept;

    struct ActorFrameData
    {
        ActorFrameData(Actor& actor, bool inert, bool waterCollision, float slowFall, float waterlevel, bool isPlayer);
        osg::Vec3f mPosition;
        osg::Vec3f mInertia;
        const btCollisionObject* mStandingOn;
        bool mIsOnGround;
        bool mIsOnSlope;
        bool mWalkingOnWater;
        const bool mInert;
        btCollisionObject* mCollisionObject;
        const float mSwimLevel;
        const float mSlowFall;
        osg::Vec2f mRotation;
        osg::Vec3f mMovement;
        osg::Vec3f mLastStuckPosition;
        const float mWaterlevel;
        const float mHalfExtentsZ;
        float mOldHeight;
        unsigned int mStuckFrames;
        const bool mFlying;
        const bool mWasOnGround;
        const bool mIsAquatic;
        const bool mWaterCollision;
        const bool mSkipCollisionDetection;
        const bool mIsPlayer;
    };

    struct ProjectileFrameData
    {
        explicit ProjectileFrameData(Projectile& projectile);
        osg::Vec3f mPosition;
        osg::Vec3f mMovement;
        const btCollisionObject* mCaster;
        const btCollisionObject* mCollisionObject;
        Projectile* mProjectile;
    };

    struct WorldFrameData
    {
        WorldFrameData();
        bool mIsInStorm;
        osg::Vec3f mStormDirection;
    };

    template <class Ptr, class FrameData>
    class SimulationImpl
    {
    public:
        explicit SimulationImpl(const std::weak_ptr<Ptr>& ptr, FrameData&& data)
            : mPtr(ptr)
            , mData(data)
        {
        }

        std::optional<std::pair<std::shared_ptr<Ptr>, std::reference_wrapper<FrameData>>> lock()
        {
            if (auto locked = mPtr.lock())
                return { { std::move(locked), std::ref(mData) } };
            return std::nullopt;
        }

    private:
        std::weak_ptr<Ptr> mPtr;
        FrameData mData;
    };

    using ActorSimulation = SimulationImpl<Actor, ActorFrameData>;
    using ProjectileSimulation = SimulationImpl<Projectile, ProjectileFrameData>;
    using Simulation = std::variant<ActorSimulation, ProjectileSimulation>;

    class PhysicsSystem : public IPhysicsBackend
    {
    public:
        PhysicsSystem(Resource::ResourceSystem* resourceSystem, osg::ref_ptr<osg::Group> parentNode);
        ~PhysicsSystem() override;

        // IPhysicsBackend identity (compile-time fixed for this impl).
        std::string_view name() const override { return physicsBackendName(); }

        Resource::BulletShapeManager* getShapeManager() override;
        float getPhysicsDt() const override { return mPhysicsDt; }
        std::vector<std::pair<const Object*, bool>> getAnimatedObjects() const override;

        void enableWater(float height) override;
        void setWaterHeight(float height) override;
        void disableWater() override;

        void addObject(const MWWorld::Ptr& ptr, VFS::Path::NormalizedView mesh, osg::Quat rotation,
            int collisionType = CollisionType_World) override;
        void addActor(const MWWorld::Ptr& ptr, VFS::Path::NormalizedView mesh) override;

        int addProjectile(
            const MWWorld::Ptr& caster, const osg::Vec3f& position, VFS::Path::NormalizedView mesh, bool computeRadius) override;
        void setCaster(int projectileId, const MWWorld::Ptr& caster) override;
        void removeProjectile(const int projectileId) override;

        void updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& updated) override;

        Actor* getActor(const MWWorld::Ptr& ptr) override;
        const Actor* getActor(const MWWorld::ConstPtr& ptr) const override;

        const Object* getObject(const MWWorld::ConstPtr& ptr) const override;

        Projectile* getProjectile(int projectileId) const override;

        // Object or Actor
        void remove(const MWWorld::Ptr& ptr) override;

        void updateScale(const MWWorld::Ptr& ptr) override;
        void updateRotation(const MWWorld::Ptr& ptr, osg::Quat rotate) override;
        void updatePosition(const MWWorld::Ptr& ptr) override;

        void addHeightField(const float* heights, int x, int y, int size, int verts, float minH, float maxH,
            const osg::Object* holdObject) override;

        void removeHeightField(int x, int y) override;

        const HeightField* getHeightField(int x, int y) const override;

        bool toggleCollisionMode() override;

        /// Determine new position based on all queued movements, then clear the list.
        void stepSimulation(
            float dt, bool skipSimulation, osg::Timer_t frameStart, unsigned int frameNumber, osg::Stats& stats) override;

        /// Apply new positions to actors
        void moveActors() override;
        void debugDraw() override;

        std::vector<MWWorld::Ptr> getCollisions(const MWWorld::ConstPtr& ptr, int collisionGroup,
            int collisionMask) const; ///< get handles this object collides with
        std::vector<ContactPoint> getCollisionsPoints(
            const MWWorld::ConstPtr& ptr, int collisionGroup, int collisionMask) const override;
        osg::Vec3f traceDown(const MWWorld::Ptr& ptr, const osg::Vec3f& position, float maxHeight) override;

        /// @param ignore Optional, a list of Ptr to ignore in the list of results. targets are actors to filter for,
        /// ignoring all other actors.
        RayCastingResult castRay(const osg::Vec3f& from, const osg::Vec3f& to,
            const std::vector<MWWorld::ConstPtr>& ignore = {}, const std::vector<MWWorld::Ptr>& targets = {},
            int mask = CollisionType_Default, int group = 0xff) const override;
        using RayCastingInterface::castRay;

        RayCastingResult castSphere(const osg::Vec3f& from, const osg::Vec3f& to, float radius,
            int mask = CollisionType_Default, int group = 0xff) const override;

        /// Return true if actor1 can see actor2.
        bool getLineOfSight(const MWWorld::ConstPtr& actor1, const MWWorld::ConstPtr& actor2) const override;

        bool isOnGround(const MWWorld::Ptr& actor) override;

        bool canMoveToWaterSurface(const MWWorld::ConstPtr& actor, const float waterlevel) override;

        /// Get physical half extents (scaled) of the given actor.
        osg::Vec3f getHalfExtents(const MWWorld::ConstPtr& actor) const override;

        /// Get physical half extents (not scaled) of the given actor.
        osg::Vec3f getOriginalHalfExtents(const MWWorld::ConstPtr& actor) const override;

        /// @see MWPhysics::Actor::getRenderingHalfExtents
        osg::Vec3f getRenderingHalfExtents(const MWWorld::ConstPtr& actor) const override;

        /// Get the position of the collision shape for the actor. Use together with getHalfExtents() to get the
        /// collision bounds in world space.
        /// @note The collision shape's origin is in its center, so the position returned can be described as center of
        /// the actor collision box in world space.
        osg::Vec3f getCollisionObjectPosition(const MWWorld::ConstPtr& actor) const override;

        /// Get bounding box in world space of the given object.
        osg::BoundingBox getBoundingBox(const MWWorld::ConstPtr& object) const override;

        /// Queues velocity movement for a Ptr. If a Ptr is already queued, its velocity will
        /// be overwritten. Valid until the next call to stepSimulation
        void queueObjectMovement(const MWWorld::Ptr& ptr, const osg::Vec3f& velocity) override;

        /// Clear the queued movements list without applying.
        void clearQueuedMovement() override;

        /// Return true if \a actor has been standing on \a object in this frame
        /// This will trigger whenever the object is directly below the actor.
        /// It doesn't matter if the actor is stationary or moving.
        bool isActorStandingOn(const MWWorld::Ptr& actor, const MWWorld::ConstPtr& object) const override;

        /// Get the handle of all actors standing on \a object in this frame.
        void getActorsStandingOn(const MWWorld::ConstPtr& object, std::vector<MWWorld::Ptr>& out) const override;

        /// Return true if an object of the given type has collided with this object
        bool isObjectCollidingWith(const MWWorld::ConstPtr& object, ScriptedCollisionType type) const override;

        /// Get the handle of all actors colliding with \a object in this frame.
        void getActorsCollidingWith(const MWWorld::ConstPtr& object, std::vector<MWWorld::Ptr>& out) const override;

        bool toggleDebugRendering() override;

        /// Mark the given object as a 'non-solid' object. A non-solid object means that
        /// \a isOnSolidGround will return false for actors standing on that object.
        void markAsNonSolid(const MWWorld::ConstPtr& ptr) override;

        bool isOnSolidGround(const MWWorld::Ptr& actor) const override;

        void updateAnimatedCollisionShape(const MWWorld::Ptr& object) override;

        template <class Function>
        void forEachAnimatedObject(Function&& function) const
        {
            std::for_each(mAnimatedObjects.begin(), mAnimatedObjects.end(), function);
        }

        bool isAreaOccupiedByOtherActor(
            const MWWorld::LiveCellRefBase* actor, const osg::Vec3f& position, float radius) const override;

        void reportStats(unsigned int frameNumber, osg::Stats& stats) const override;

        // IPhysicsBackend public signature: takes osg::Vec3f so the
        // interface stays Bullet-type-free. The internal projectile
        // path keeps a btVector3 overload for convenience.
        void reportCollision(const osg::Vec3f& position, const osg::Vec3f& normal) override;
        void reportCollision(const btVector3& position, const btVector3& normal);

        float mPhysicsDt;

    private:
        void updateWater();

        void prepareSimulation(bool willSimulate, std::vector<Simulation>& simulations);

        std::unique_ptr<btBroadphaseInterface> mBroadphase;
        std::unique_ptr<btDefaultCollisionConfiguration> mCollisionConfiguration;
        std::unique_ptr<btCollisionDispatcher> mDispatcher;
        std::unique_ptr<btCollisionWorld> mCollisionWorld;
        std::unique_ptr<PhysicsTaskScheduler> mTaskScheduler;

        std::unique_ptr<Resource::BulletShapeManager> mShapeManager;
        Resource::ResourceSystem* mResourceSystem;

        using ObjectMap = std::unordered_map<const MWWorld::LiveCellRefBase*, std::shared_ptr<Object>>;
        ObjectMap mObjects;

        std::map<Object*, bool> mAnimatedObjects; // stores pointers to elements in mObjects

        ActorMap mActors;

        using ProjectileMap = std::map<int, std::shared_ptr<Projectile>>;
        ProjectileMap mProjectiles;

        using HeightFieldMap = std::map<std::pair<int, int>, std::unique_ptr<HeightField>>;
        HeightFieldMap mHeightFields;

        bool mDebugDrawEnabled;

        float mTimeAccum;

        unsigned int mProjectileId;

        float mWaterHeight;
        bool mWaterEnabled;

        std::unique_ptr<btCollisionObject> mWaterCollisionObject;
        std::unique_ptr<btCollisionShape> mWaterCollisionShape;

        std::unique_ptr<MWRender::DebugDrawer> mDebugDrawer;

        osg::ref_ptr<osg::Group> mParentNode;

        std::size_t mSimulationsCounter = 0;
        std::array<std::vector<Simulation>, 2> mSimulations;
        std::vector<std::pair<MWWorld::Ptr, osg::Vec3f>> mActorsPositions;

        PhysicsSystem(const PhysicsSystem&);
        PhysicsSystem& operator=(const PhysicsSystem&);
    };
}

#endif
