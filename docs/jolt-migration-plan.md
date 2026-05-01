# Jolt Physics migration plan

A phased, build-safe-at-every-step migration from Bullet Physics to
Jolt Physics. Default builds keep using Bullet until the Jolt path is
fully ready and benched against vanilla.

The migration is gated by the CMake option `OPENMW_PHYSICS_BACKEND`
(values: `bullet` (default) | `jolt`). Bullet stays linked in both
modes — `components/detournavigator` and `components/bullethelpers`
consume `btCollisionShape` directly to feed Recast, so Bullet acts as
"asset/shape source of truth" even when the active simulation runs on
Jolt.

## Status snapshot

| Phase | Step | Status | Notes |
|-------|------|--------|-------|
| 1 | CMake flag | ✅ done | commit `1a74fc9236` |
| 2 | Compile-time scaffold (`physicsbackend.hpp`) | ✅ done | commit `1a74fc9236` |
| 3a | `IPhysicsBackend` interface header (additive) | ✅ done | commit `2d67e3b331` |
| 3b | Interface extended to full public API | ✅ done | commit `6d248e450b` |
| 3c | `PhysicsSystem : public IPhysicsBackend` | ✅ done | commit `8997ef28ce` |
| 3c | `override` cleanup (43 methods) | ✅ done | commit `699610e678` |
| - | `extern/JoltPhysics` submodule | ✅ cloned | pinned at `04587a3e` |
| 4 | `JoltPhysicsSystem` skeleton + factory | ✅ done |
| 5 | Jolt-side world bootstrap (BroadPhaseLayers, ObjectLayers) | ✅ done |
| 6a | Shape converter — primitive Bullet shapes | ✅ done |
| 6b1 | Shape converter — compound | ✅ done |
| 6b2 | Shape converter — triangle mesh | ✅ done |
| 6c | Height fields + water + addObject wiring | ✅ done |
| 7a | JoltActor scaffold (`CharacterVirtual` wrapper) | ✅ done |
| 7b | `addActor` / `remove` actor wiring | ✅ done |
| 7c | `stepSimulation` -> `ExtendedUpdate` per actor | ✅ done |
| 7d | state queries (isOnGround, position, half extents) | ✅ done |
| 7e | `queueObjectMovement` + `moveActors` | ✅ done |
| 7f | Vanilla parity tuning (slope, water, stuck recovery) | ⏳ |
| 8a | Spatial queries (castRay, castSphere, getLineOfSight, traceDown) + projectile bodies | ✅ done |
| 8b | Hit normal extraction in castRay/castSphere | ✅ done |
| 8c | Body owner map + castRay ignore filter + mHitObject (objects) | ✅ done |
| 8c′ | Owner-map for projectiles + sphere-cast ignore filter | ✅ done |
| 8d | mInnerBodyShape on JoltActor (actors visible to ray casts) | ✅ done |
| 8e | All remaining `IPhysicsBackend` stubs (updatePtr/Pos/Rot, BBox, etc.) | ✅ done |
| 9 | Detour bridge — verified inert under Jolt (Bullet stays as Recast feeder) | ✅ done |
| 11 | Lua API audit — verified routes through `RayCastingInterface` (no code change) | ✅ done |
| 10a | Animated-collider tracking + on-demand shape rebuild | ✅ done |
| 10b | IPhysicsObject abstraction (Object inherits, navigator takes interface) | ✅ done |
| 10c | AnimatedObjectEntry implements IPhysicsObject (navigator wired under Jolt) | ✅ done |
| 11 | Lua API surface audit | ⏳ |
| 12 | Bench + correctness regression suite | ⏳ |
| 13 | Default flip + cleanup | ⏳ |

Total remaining estimate: ~20 dev-days. Phases 7 (CharacterVirtual)
and 9 (Detour bridge) are the highest-risk; everything else is
straightforward porting with deterministic test cases.

---

## Phase 4 — `JoltPhysicsSystem` skeleton

**Estimate:** 1 day
**Build safety:** green under both backends; the new file compiles
to an empty translation unit when `OPENMW_PHYSICS_USES_JOLT == 0`.

**Steps:**

1. Create `apps/openmw/mwphysics/joltphysicssystem.hpp` declaring
   `class JoltPhysicsSystem : public IPhysicsBackend`. Whole header
   guarded by `#if OPENMW_PHYSICS_USES_JOLT`.
2. Create `apps/openmw/mwphysics/joltphysicssystem.cpp` with stub
   implementations: every method throws
   `std::logic_error("JoltPhysicsSystem: <method> not implemented")`
   except `name()` (returns `physicsBackendName()`) and the destructor.
   Same `#if` guard.
3. Add `joltphysicssystem` to `add_openmw_dir(mwphysics ...)` in
   `apps/openmw/CMakeLists.txt`. The .cpp compiles to nothing under
   Bullet; the linker drops it.
4. Add a factory function in `physicsbackend.hpp`:
   ```cpp
   inline std::unique_ptr<IPhysicsBackend>
   makePhysicsBackend(Resource::ResourceSystem* rs, osg::ref_ptr<osg::Group> parent);
   ```
   The implementation lives in a tiny new
   `physicsbackend.cpp` so the header stays free of heavy includes.
5. Switch the single instantiation site (in `mwworld/world.cpp` /
   `worldimp.cpp`) from `new PhysicsSystem(...)` to
   `makePhysicsBackend(...)` and store as `IPhysicsBackend*`.
6. Verify build green under both `-DOPENMW_PHYSICS_BACKEND=bullet`
   and `-DOPENMW_PHYSICS_BACKEND=jolt`. Under jolt the engine
   compiles + links; the first physics call at runtime throws and
   we know the dispatch works.

**Done when:** running with `bullet` is identical to today; running
with `jolt` aborts cleanly on the first physics call with a
"not implemented" message naming the method.

---

## Phase 5 — Jolt-side world bootstrap

**Estimate:** ½ day
**Risk:** low (Jolt's HelloWorld example is the template)

**Steps:**

1. `JoltPhysicsSystem` constructor:
   - `JPH::RegisterDefaultAllocator()`
   - `JPH::Factory::sInstance = new JPH::Factory()`
   - `JPH::RegisterTypes()`
   - Allocate temp + job system (reuse OpenMW's `WorkQueue`).
   - Build a `JPH::PhysicsSystem` with sane MaxBodies (~10240),
     MaxBodyPairs (~65536), MaxContactConstraints (~10240).
2. Define `BroadPhaseLayers` (NON_MOVING, MOVING) and
   `ObjectLayers` (corresponding to `CollisionType_*` from
   `mwphysics/collisiontype.hpp`).
3. Wire `JPH::ObjectLayerPairFilter` so OpenMW's existing
   collision groups/masks map cleanly: World, Actor, Projectile,
   Door, HeightMap, Water, Camera, ActorAndPlayer.
4. Set up `JPH::ContactListener` and
   `JPH::CharacterContactListener` (no-op stubs for now;
   populated in phase 7/8).

**Done when:** `JoltPhysicsSystem` constructs without throwing, and
`stepSimulation` runs an empty Jolt world for one tick.

---

## Phase 6 — Static colliders (objects, height fields, water)

**Estimate:** 1.5 days
**Risk:** medium — shape conversion `btCollisionShape → JPH::Shape`
is the bulk of the work. Recast still consumes the Bullet shape
verbatim (see phase 9).

**Steps:**

1. Implement `addObject` / `remove` / `updatePosition` /
   `updateRotation` / `updateScale`.
2. Shape converter (new file `joltshapeconverter.cpp`): walk the
   `btCollisionShape` hierarchy from the existing
   `Resource::BulletShape`, emit equivalent `JPH::Shape`:
   - `btBoxShape` → `JPH::BoxShape`
   - `btSphereShape` → `JPH::SphereShape`
   - `btCylinderShape` → `JPH::CylinderShape`
   - `btCapsuleShape` → `JPH::CapsuleShape`
   - `btCompoundShape` → `JPH::StaticCompoundShape`
   - `btBvhTriangleMeshShape` → `JPH::MeshShape`
3. Height field converter: `btHeightfieldTerrainShape` → `JPH::HeightFieldShape`.
   Bullet stores Z-up float heights; Jolt expects the same. One-pass copy.
4. Water: `JoltPhysicsSystem::enableWater` becomes a sensor body
   filtering `CollisionType_Water` for `CharacterVirtual`'s
   buoyancy queries.

**Done when:** loading the player's home cell in Seyda Neen with
`-DOPENMW_PHYSICS_BACKEND=jolt` shows correct collision against
walls/floor/water surface (no actor movement yet).

---

## Phase 7 — Actor controller (CharacterVirtual ↔ MovementSolver)

**Estimate:** 3 days
**Risk:** highest. Vanilla MW physics has subtle behaviour around
slopes, slides, water entry/exit, ledge grabs, and stair stepping
that the OpenMW `MovementSolver` reproduces faithfully. Jolt's
`CharacterVirtual` covers ~80% of this out of the box; the gap
needs to be re-tuned to match vanilla.

**Steps:**

1. `JoltActor` class wrapping `JPH::CharacterVirtual` per actor.
2. Map `MovementSolver` inputs (queued velocities, water level,
   slow-fall, swim/fly state, NPC vs player) onto Jolt's
   `ExtendedUpdate` parameters.
3. Reproduce the vanilla "stuck frame" recovery (`mLastStuckPosition`
   in `ActorFrameData`) — Jolt's character can soft-deadlock against
   geometry; a 4-frame retry path is needed.
4. Slope behaviour: Jolt's max slope angle vs MW's
   `fSlopeBraking` GMST. Pick whichever produces vanilla-matching
   AI pathing on the test cells.
5. Water surface: `mIsAquatic` actors swim through the water layer;
   non-aquatic actors get the "stuck at surface" treatment.
6. Step over: Jolt's `mStepUp` vs MW's `MaxStepHeight = 50` units.
   Adjust per-actor based on half-extents.

**Done when:** the player walks, jumps, swims, and climbs Balmora's
streets identically to a Bullet build (within 5 cm position drift
over 30 s) and AI pathfinding doesn't regress on the same routes.

---

## Phase 8 — Projectiles, raycasts, sphere casts

**Estimate:** 1 day

**Steps:**

1. `addProjectile` → small `JPH::Body` with `JPH::SphereShape` and
   `mIsSensor = true`. Updated each step like a regular dynamic body.
2. `castRay` → `JPH::PhysicsSystem::GetNarrowPhaseQuery().CastRay`
   plus a custom `RayFilter` that honours OpenMW's
   `(ignore, targets, mask, group)` semantics.
3. `castSphere` → `CastShape` with a `JPH::SphereShape` swept from
   `from` to `to`.
4. `getLineOfSight` → a single `castRay` with the actor head/eye
   offsets; reuse the existing logic from `physicssystem.cpp`.
5. `traceDown` → `castRay` along world Z down by `maxHeight`.

**Done when:** spell hit detection, arrows, and AI line-of-sight
behave correctly in Seyda Neen and Balmora. Ranged combat damage
matches Bullet baseline within ±5%.

---

## Phase 9 — Detour bridge (verified inert under Jolt)

**Status:** verified, no code change needed.

A grep across `components/detournavigator/`:
- 9 files reference `btCollisionShape` / `btCollisionObject`.
- 0 files reference `PhysicsSystem`, `JoltPhysicsSystem`, `JoltActor`, or any other runtime-physics class.

The Detour navigator pulls collision shapes from
`Resource::BulletShape` (the asset cache) directly; it never asks
the runtime simulator for them. Switching the runtime simulator to
Jolt has zero impact on navmesh generation: same shapes go in, same
triangle soup comes out, identical navmesh artifacts.

Phase 6c's `JoltPhysicsSystem::addObject` calls
`mShapeManager->getInstance(...)` for the same `BulletShape` the
Bullet path uses, then builds the Jolt-side runtime shape *on top
of that*. The `BulletShape` itself stays in the resource cache and
is consumed by Recast in parallel.

**Done when:** ✅ verified — there's nothing to do.

The shape pipeline stays:
```
NIF → BulletShape → btCollisionShape → Recast triangles
                                    ↘
                                     JoltShapeConverter → JPH::Shape (runtime)
```

---

## Phase 10 — Animated / skinned shape sync

**Estimate:** 3 days
**Risk:** medium — animated dwemer doors, levers, hanging meat,
moving platforms all rely on per-frame shape updates.

**Steps:**

1. For each `mAnimatedObjects` entry in `JoltPhysicsSystem`,
   re-extract the skinned vertex positions each frame and feed them
   to the matching `JPH::MeshShape` via
   `JPH::MutableCompoundShape::ModifyShape`.
2. Throttle: skip the update when the shape's vertex deltas are
   below ~0.1 unit (vanilla MW does the same to avoid spamming
   Bullet's bvh recompute).
3. Validate against a known set: dwemer pistons in Bamz-Amschend,
   the silt strider in Seyda Neen, doors in Balmora.

**Done when:** animated colliders produce no actor jitter or
fall-through, and the per-frame physics cost stays within ±15 % of
Bullet on the same scene.

---

## Phase 11 — Lua API surface audit

**Estimate:** ½ day

`mwlua` currently exposes physics queries (raycast, sphere cast,
collision tests). Verify that every binding in `mwlua/luabindings/*`
that talks to `MWPhysics::PhysicsSystem` calls a method that's now
on `IPhysicsBackend`. The `override` cleanup in phase 3c makes this
mechanical: grep for `mPhysics->` and confirm each callee has
`override` on its declaration.

**Done when:** every script-facing physics binding routes through
the interface, and the Jolt path passes the existing Lua test
suite (`tests/lua/`) without changes.

---

## Phase 12 — Bench + correctness regression suite

**Estimate:** 2 days

**Steps:**

1. Build a deterministic playback test: record a player input
   sequence (move, jump, fall, swim) for Seyda Neen → Hla Oad and
   replay it under both backends. Capture position trajectories.
2. Compare trajectories: tolerance ±5 cm cumulative drift over the
   recorded session.
3. AI path stability: trigger known NPC patrols (Seyda Neen guards,
   Balmora silt strider riders) and compare path length / time.
4. Frame budget: 60 s of busy combat in Vivec Foreign Quarter
   under both backends, stat the physics step time.

**Done when:** the Jolt build produces functionally identical
behaviour and is within ±20 % of the Bullet build's per-frame
physics time.

---

## Phase 13 — Default flip + cleanup

**Estimate:** ½ day

1. Change `OPENMW_PHYSICS_BACKEND` default from `bullet` to `jolt`
   in the top-level `CMakeLists.txt`.
2. Update `docs/source/install/index.rst` install notes.
3. Keep Bullet as the asset-side shape source (Recast input) — do
   NOT remove the Bullet dependency.
4. Open a fallback ticket: if a regression is found in the wild,
   users can rebuild with `-DOPENMW_PHYSICS_BACKEND=bullet` to
   revert to vanilla behaviour while we patch.

**Done when:** the default `cmake ..` build runs Jolt and the test
cells from phase 12 still pass.

---

## Things that are deliberately not migrated

- `components/bullethelpers` — stays Bullet. Used by the asset
  pipeline (NIF parsing → shape building) which is decoupled from
  the runtime sim.
- `components/detournavigator` — stays Bullet. See phase 9.
- The shape resource cache (`Resource::BulletShape`) — same.

The goal is to swap the *runtime simulator*, not the *asset
pipeline*. Bullet stays linked indefinitely in the smaller, asset-
serving role.

## Reference — current commit chain

```
<phase 4 commit>     Jolt phase 4: JoltPhysicsSystem skeleton + factory + caller switchover
699610e678          Jolt phase 3c follow-up: add override on PhysicsSystem interface methods
8997ef28ce          Jolt phase 3c: PhysicsSystem inherits from IPhysicsBackend
6d248e450b          Jolt phase 3b: extend IPhysicsBackend to the full public surface
2d67e3b331          Jolt phase 3a: add IPhysicsBackend interface header (additive, build-safe)
1a74fc9236          PBR pipeline polish + Jolt Physics compile-time scaffold
```
