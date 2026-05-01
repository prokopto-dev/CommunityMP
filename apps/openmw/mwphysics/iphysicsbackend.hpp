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
// Scope deliberately excluded from the interface (for now):
//   - castRay / castSphere — RayCastingResult.mHitObject is a raw
//     const btCollisionObject*. Step 3b will replace it with an
//     opaque PhysicsBodyHandle. Until then, RayCastingInterface (the
//     existing virtual base in raycasting.hpp) stays the abstraction
//     point for ray queries.
//   - getCollisions / getCollisionsPoints — same Bullet leak via
//     ContactPoint.
//   - getActor / getObject / getProjectile — return raw pointers to
//     concrete classes that hold btCollisionObject internally. Will
//     stay PhysicsSystem-specific until callers learn to ask the
//     backend for handle queries instead.
//
// Once 3b lands the handle wrapper, every entry below marked [pending
// wrapper] can move into this header and PhysicsSystem can finally
// inherit from IPhysicsBackend in 3c.

#include <memory>
#include <string_view>
#include <vector>

#include <osg/Quat>
#include <osg/Stats>
#include <osg/Timer>
#include <osg/Vec3f>

#include <components/vfs/pathutil.hpp>

#include "../mwworld/ptr.hpp"

#include "collisiontype.hpp"

namespace MWPhysics
{
    class HeightField;

    // Pure-virtual surface for the backend. Lifetime is owned by
    // PhysicsSystem; every call must be safe from the main game
    // thread. Callbacks run on the physics task scheduler thread —
    // see mtphysics.hpp for the threading contract.
    class IPhysicsBackend
    {
    public:
        virtual ~IPhysicsBackend() = default;

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

        // ----- Pending wrapper work — see file comment -----------------
        // Listed here for future-me / next reviewer. These would join
        // the interface in phase 3b once PhysicsBodyHandle exists.
        //
        //   castRay / castSphere       — needs handle-typed RayCastingResult
        //   getLineOfSight             — currently fine; just lives on
        //                                RayCastingInterface, so leaving
        //                                it there avoids a double-virtual
        //                                hop.
        //   getCollisions /             — handle-typed ContactPoint
        //   getCollisionsPoints
        //   traceDown                  — clean signature; can lift today
        //                                but cluster with the spatial
        //                                queries above for the next pass
        //   getActor / getObject /     — return concrete classes that
        //   getProjectile                store btCollisionObject; their
        //                                clients reach into Bullet
        //                                directly today
        //   isOnGround                 — clean; same cluster
        //   canMoveToWaterSurface      — clean; same cluster
    };
}

#endif
