#ifndef OPENMW_MWPHYSICS_IPHYSICSOBJECT_H
#define OPENMW_MWPHYSICS_IPHYSICSOBJECT_H

// Minimal abstraction over the per-object handles the navigator
// consumes when a static collider's shape changes. The Bullet path
// has its own concrete `Object` class (mwphysics/object.hpp) that
// implements this; the Jolt path uses `JoltObject` (in
// mwphysics/joltobject.hpp) so getAnimatedObjects() can hand back
// real entries under either backend.
//
// The transform returns a btTransform — that's not a leak of the
// runtime simulator into callers, it's the navigator's input type.
// The Jolt path synthesises a btTransform from its body's position
// + rotation; the Bullet path returns the body's transform directly.

#include <LinearMath/btTransform.h>

#include "../mwworld/ptr.hpp"

namespace Resource
{
    class BulletShapeInstance;
}

namespace MWPhysics
{
    class IPhysicsObject
    {
    public:
        virtual ~IPhysicsObject() = default;
        virtual const Resource::BulletShapeInstance* getShapeInstance() const = 0;
        virtual MWWorld::Ptr getPtr() const = 0;
        virtual btTransform getTransform() const = 0;
    };
}

#endif
