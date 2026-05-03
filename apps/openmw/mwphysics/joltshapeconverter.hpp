#ifndef OPENMW_MWPHYSICS_JOLTSHAPECONVERTER_H
#define OPENMW_MWPHYSICS_JOLTSHAPECONVERTER_H

#include "physicsbackend.hpp"

#if OPENMW_PHYSICS_USES_JOLT

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

class btCollisionShape;

namespace MWPhysics
{
    // Convert a Bullet collision shape into the equivalent Jolt
    // shape. The Bullet shape stays the asset-side source of truth
    // (Resource::BulletShape), is consumed by Recast for navmesh,
    // and is also fed through this function to produce the runtime
    // Jolt body's shape.
    //
    // Returns nullptr if the shape type is unsupported or if shape
    // settings rejected the conversion. Callers should log + skip
    // unsupported entries — they'll show up missing collision but
    // won't crash the engine.
    //
    // Phase 6a (this file's first landing) handles the "primitive"
    // shape types: box, sphere, capsule, cylinder. Phase 6b adds
    // compound + triangle mesh; phase 6c adds height field.
    JPH::RefConst<JPH::Shape> convertBulletShape(const btCollisionShape* shape);

    // Build a single ConvexHullShape from every triangle vertex
    // reachable through processAllTriangles on the Bullet shape (or
    // recursively through compound children). Used for Phase 6 of
    // docs/imgui-overlay-plan.md to give dynamic rigid bodies a
    // mesh-conforming collider — Jolt forbids MeshShape for Dynamic
    // motion type, so a hull is the closest practical fit. Concave
    // detail is lost (the hull "fills in" indentations) but for
    // barrels / crates / pots that's fine.
    JPH::RefConst<JPH::Shape> extractConvexHull(const btCollisionShape* shape);
}

#endif

#endif
