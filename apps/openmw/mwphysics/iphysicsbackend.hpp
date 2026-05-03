#ifndef OPENMW_MWPHYSICS_IPHYSICSBACKEND_H
#define OPENMW_MWPHYSICS_IPHYSICSBACKEND_H

// IPhysicsBackend declares the abstract API the rest of OpenMW would
// consume regardless of which simulation library is doing the work
// (Bullet today; Jolt tomorrow once the OPENMW_PHYSICS_BACKEND=jolt
// build path lights up).
//
// Status: Phase 3a of the Jolt migration — this header is purely
// additive. No caller in the tree includes it yet. PhysicsSystem
// continues to be the concrete entry point, and the Bullet impl
// stays the only behaviour. The point of landing this now is:
//   1. Force the team (and me) to stop and write down which methods
//      are clean (no Bullet type leakage) and which ones still need
//      handle wrapping before they can sit on an interface.
//   2. Give phase 3b a stable target to refactor toward — the
//      methods listed below with [pending wrapper] notes are the
//      exact set that requires opaque-handle work.
//   3. Keep the build green: this file compiles standalone, exports
//      a pure abstract class, and declares no symbols that link.
//
// Re-audited in phase 3b: a grep across mwworld/, mwmechanics/,
// mwrender/, mwgui/, mwbase/, mwlua/ found zero references to
// btCollisionObject / btRigidBody (one stale #include in scene.cpp;
// no actual use). The Bullet types I previously thought leaked
// (`mStandingOn`, `mCollisionObject` on FrameData, etc.) are all
// internal to mwphysics/ — they're consumed by movementsolver.cpp,
// mtphysics.cpp, and the FrameData structs themselves, none of
// which sit on the public surface. So the interface below grows to
// include every public PhysicsSystem method without needing an
// opaque-handle wrapper. Outside callers already speak in
// MWWorld::Ptr / RayCastingResult / ContactPoint, all of which are
// physics-library-agnostic.
//
// One remaining Bullet boundary: PhysicsSystem::reportCollision
// takes (btVector3, btVector3). The single external caller in
// mwworld/worldimp.cpp already converts from osg::Vec3f via
// Misc::Convert::toBullet just to call it, so the Bullet types
// there are gratuitous. The interface declares the osg::Vec3f
// signature; the Bullet impl will do the conversion internally,
// the Jolt impl will use the values directly.
//
// Phase 3c: PhysicsSystem inherits from IPhysicsBackend and a
// factory in physicsbackend.hpp returns the right concrete class
// based on OPENMW_PHYSICS_USES_JOLT / _BULLET. No call site changes.

#include <functional>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <osg/BoundingBox>
#include <osg/Object>
#include <osg/Quat>
#include <osg/Stats>
#include <osg/Timer>
#include <osg/Vec3f>

namespace Resource
{
    class BulletShapeManager;
}

#include <components/vfs/pathutil.hpp>

#include "../mwworld/ptr.hpp"

#include "collisiontype.hpp"
#include "raycasting.hpp"

namespace MWPhysics
{
    class HeightField;
    class IPhysicsActor;
    class IPhysicsObject;
    class Object;
    class Projectile;
    struct ContactPoint;
    enum ScriptedCollisionType : char; // defined in object.hpp

    // Pure-virtual surface for the backend. Lifetime is owned by
    // PhysicsSystem; every call must be safe from the main game
    // thread. Callbacks run on the physics task scheduler thread —
    // see mtphysics.hpp for the threading contract.
    //
    // Inherits RayCastingInterface so castRay / castSphere /
    // getLineOfSight are part of the same vtable — that's already the
    // shape PhysicsSystem speaks today, and it lets a single override
    // in the concrete impl satisfy both abstractions without diamond.
    class IPhysicsBackend : public RayCastingInterface
    {
    public:
        ~IPhysicsBackend() override = default;

        // ----- Identity --------------------------------------------------
        // Returns "Bullet" or "Jolt". Used in the F3 stats overlay and
        // diagnostic logs. Compile-time-resolvable but kept virtual so
        // a hot-swap test harness can flip backends.
        virtual std::string_view name() const = 0;

        // ----- Water plane ----------------------------------------------
        virtual void enableWater(float height) = 0;
        virtual void setWaterHeight(float height) = 0;
        virtual void disableWater() = 0;

        // ----- Object / actor / projectile lifecycle -------------------
        virtual void addObject(const MWWorld::Ptr& ptr, VFS::Path::NormalizedView mesh, osg::Quat rotation,
            int collisionType = CollisionType_World)
            = 0;
        virtual void addActor(const MWWorld::Ptr& ptr, VFS::Path::NormalizedView mesh) = 0;

        // Phase 6 of docs/imgui-overlay-plan.md — promote an existing
        // static object to a dynamic rigid body with a primitive
        // collider. Default no-op so the Bullet backend stays
        // unchanged; only Jolt implements it for now.
        enum class DynamicShape
        {
            Box,
            Cylinder,
            Sphere,
            Mesh, // ConvexHull extracted from the source NIF
        };
        virtual void promoteToDynamic(const MWWorld::Ptr& /*ptr*/, DynamicShape /*shape*/,
            const osg::Vec3f& /*halfExtents*/, float /*mass*/)
        {
        }

        virtual int addProjectile(const MWWorld::Ptr& caster, const osg::Vec3f& position,
            VFS::Path::NormalizedView mesh, bool computeRadius)
            = 0;
        virtual void setCaster(int projectileId, const MWWorld::Ptr& caster) = 0;
        virtual void removeProjectile(int projectileId) = 0;

        // Generic remove — handles both Object and Actor. Caller must
        // have added the Ptr previously.
        virtual void remove(const MWWorld::Ptr& ptr) = 0;

        // ----- Ptr migration (cell change, save load) ------------------
        virtual void updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& updated) = 0;
        virtual void updateScale(const MWWorld::Ptr& ptr) = 0;
        virtual void updateRotation(const MWWorld::Ptr& ptr, osg::Quat rotate) = 0;
        virtual void updatePosition(const MWWorld::Ptr& ptr) = 0;

        // ----- Static terrain ------------------------------------------
        virtual void addHeightField(const float* heights, int x, int y, int size, int verts, float minH, float maxH,
            const osg::Object* holdObject)
            = 0;
        virtual void removeHeightField(int x, int y) = 0;
        virtual const HeightField* getHeightField(int x, int y) const = 0;

        // ----- Per-frame step ------------------------------------------
        // Determine new positions based on queued movements, advance
        // the simulation, and clear the work list. Stats are written
        // into the OSG stats slot for the F3 overlay.
        virtual void stepSimulation(
            float dt, bool skipSimulation, osg::Timer_t frameStart, unsigned int frameNumber, osg::Stats& stats)
            = 0;

        // Apply the resulting actor positions to MWWorld::Ptr.
        virtual void moveActors() = 0;

        // Toggle TCL-style noclip. Returns the new state.
        virtual bool toggleCollisionMode() = 0;

        // Visualisation hook (debug draw). May be a no-op if the
        // backend ships its own debug renderer.
        virtual void debugDraw() = 0;

        // ----- Spatial queries -----------------------------------------
        // castRay / castSphere / getLineOfSight come from
        // RayCastingInterface — see raycasting.hpp.
        //
        // Returns the list of MWWorld::Ptrs whose collision objects
        // overlap `ptr`. The Ptrs are physics-library-agnostic;
        // ContactPoint also speaks in MWWorld::Ptr / osg::Vec3f.
        virtual std::vector<MWWorld::Ptr> getCollisions(
            const MWWorld::ConstPtr& ptr, int collisionGroup, int collisionMask) const
            = 0;
        virtual std::vector<ContactPoint> getCollisionsPoints(
            const MWWorld::ConstPtr& ptr, int collisionGroup, int collisionMask) const
            = 0;

        // Drop a ray straight down from `position` for up to `maxHeight`
        // and return where it hit (or position - maxHeight if it hits
        // nothing). Used by AI placement and ground-snap helpers.
        virtual osg::Vec3f traceDown(const MWWorld::Ptr& ptr, const osg::Vec3f& position, float maxHeight) = 0;

        // ----- Actor state queries -------------------------------------
        virtual bool isOnGround(const MWWorld::Ptr& actor) = 0;
        virtual bool isOnSolidGround(const MWWorld::Ptr& actor) const = 0;
        virtual bool canMoveToWaterSurface(const MWWorld::ConstPtr& actor, float waterlevel) = 0;

        virtual osg::Vec3f getHalfExtents(const MWWorld::ConstPtr& actor) const = 0;
        virtual osg::Vec3f getOriginalHalfExtents(const MWWorld::ConstPtr& actor) const = 0;
        virtual osg::Vec3f getRenderingHalfExtents(const MWWorld::ConstPtr& actor) const = 0;
        virtual osg::Vec3f getCollisionObjectPosition(const MWWorld::ConstPtr& actor) const = 0;
        virtual osg::BoundingBox getBoundingBox(const MWWorld::ConstPtr& object) const = 0;

        // ----- Movement queue ------------------------------------------
        // Queue a velocity for `ptr`. Re-queuing overwrites. Velocities
        // are valid until the next stepSimulation call clears them.
        virtual void queueObjectMovement(const MWWorld::Ptr& ptr, const osg::Vec3f& velocity) = 0;
        virtual void clearQueuedMovement() = 0;

        // ----- Standing / collision bookkeeping ------------------------
        virtual bool isActorStandingOn(const MWWorld::Ptr& actor, const MWWorld::ConstPtr& object) const = 0;
        virtual void getActorsStandingOn(const MWWorld::ConstPtr& object, std::vector<MWWorld::Ptr>& out) const = 0;
        virtual void getActorsCollidingWith(const MWWorld::ConstPtr& object, std::vector<MWWorld::Ptr>& out) const = 0;

        // Scripted collision query — returns whether `object` was hit
        // this frame by something of `type` (Actor / Player / None).
        virtual bool isObjectCollidingWith(const MWWorld::ConstPtr& object, ScriptedCollisionType type) const = 0;

        // Mark as non-solid so isOnSolidGround returns false for actors
        // standing on it. Used for trapdoors / lifts in motion.
        virtual void markAsNonSolid(const MWWorld::ConstPtr& ptr) = 0;

        // ----- Animated collision shapes -------------------------------
        // Refresh the collision shape of an animated mesh after the
        // skinning has updated its vertices for the frame.
        virtual void updateAnimatedCollisionShape(const MWWorld::Ptr& object) = 0;

        // Test whether an actor (or another world point) is going to
        // collide with another actor in the area around `position`.
        // Used to keep AI from spawning into each other.
        virtual bool isAreaOccupiedByOtherActor(
            const MWWorld::LiveCellRefBase* actor, const osg::Vec3f& position, float radius) const
            = 0;

        // ----- Projectile / collision reporting ------------------------
        // Re-typed to osg::Vec3f relative to PhysicsSystem's signature
        // (which takes btVector3) — see the file header for the
        // rationale. The Bullet impl converts internally.
        virtual void reportCollision(const osg::Vec3f& position, const osg::Vec3f& normal) = 0;

        // ----- Diagnostics --------------------------------------------
        virtual bool toggleDebugRendering() = 0;
        virtual void reportStats(unsigned int frameNumber, osg::Stats& stats) const = 0;

        // ----- Concrete-typed lookups ---------------------------------
        // Actor, Object, Projectile are mwphysics-internal classes
        // (their public API doesn't expose Bullet types — verified by
        // grepping mwworld/mwmechanics/mwrender for getCollisionObject
        // and friends; zero hits). For the Jolt impl, JoltActor /
        // JoltObject / JoltProjectile will derive from the same
        // base classes — these accessors stay valid.
        virtual IPhysicsActor* getActor(const MWWorld::Ptr& ptr) = 0;
        virtual const IPhysicsActor* getActor(const MWWorld::ConstPtr& ptr) const = 0;
        // Returns IPhysicsObject so the Jolt path can hand back a
        // navigator-friendly view of its statics without needing to
        // construct a Bullet-flavoured Object. Callers only use the
        // virtuals declared on IPhysicsObject (getShapeInstance,
        // getTransform, getPtr) — verified by grep across mwworld /
        // mwmechanics / mwrender.
        virtual const IPhysicsObject* getObject(const MWWorld::ConstPtr& ptr) const = 0;
        virtual Projectile* getProjectile(int projectileId) const = 0;

        // ----- Concretely Bullet-flavoured but architecturally shared --
        // BulletShapeManager hosts the asset-side shape cache (NIF ->
        // btCollisionShape). It stays Bullet-typed even under Jolt
        // because the navigator's Recast feed consumes
        // btCollisionShape directly (see jolt-migration-plan.md
        // phase 9). The Jolt impl owns its own BulletShapeManager
        // for the same purpose.
        virtual Resource::BulletShapeManager* getShapeManager() = 0;

        // Internal physics tick rate (seconds). Read by worldimp's
        // time accumulator. Constant for the lifetime of the impl.
        virtual float getPhysicsDt() const = 0;

        // Snapshot of the current animated objects with a "changed
        // since last frame" flag. Used by the navigator to refresh
        // navmesh tiles when an animated collider has moved.
        // Replaces the template forEachAnimatedObject so the surface
        // can sit on a virtual. Returns IPhysicsObject so both the
        // Bullet Object and the Jolt JoltObject can fit through.
        virtual std::vector<std::pair<const IPhysicsObject*, bool>> getAnimatedObjects() const = 0;
    };
}

#endif
