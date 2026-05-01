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
}

#endif

#endif
