#include "joltshapeconverter.hpp"

#if OPENMW_PHYSICS_USES_JOLT

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>

#include <components/debug/debuglog.hpp>

namespace MWPhysics
{
    namespace
    {
        JPH::Vec3 toJolt(const btVector3& v)
        {
            return JPH::Vec3(v.x(), v.y(), v.z());
        }

        // Box: btBoxShape stores half-extents (already what Jolt
        // wants). Both libraries keep the box centred at the local
        // origin so no offset is needed.
        JPH::RefConst<JPH::Shape> convertBox(const btBoxShape* shape)
        {
            JPH::BoxShapeSettings settings(toJolt(shape->getHalfExtentsWithMargin()));
            const auto result = settings.Create();
            if (result.HasError())
            {
                Log(Debug::Warning) << "Jolt BoxShape rejected: " << result.GetError();
                return nullptr;
            }
            return result.Get();
        }

        // Sphere: identical concept. Bullet stores radius; Jolt
        // wants radius. Done.
        JPH::RefConst<JPH::Shape> convertSphere(const btSphereShape* shape)
        {
            JPH::SphereShapeSettings settings(shape->getRadius());
            const auto result = settings.Create();
            if (result.HasError())
            {
                Log(Debug::Warning) << "Jolt SphereShape rejected: " << result.GetError();
                return nullptr;
            }
            return result.Get();
        }

        // Capsule: Bullet's capsule axis is configurable
        // (btCapsuleShape*X / Y / Z); Jolt's CapsuleShape is always
        // along its local Y axis. For the Z-axis variant (the only
        // one MW uses for actor capsules) we return a CapsuleShape
        // wrapped in a 90° rotation later via RotatedTranslated.
        // For phase 6a we cover the default Y-axis case; the Z-axis
        // case is handled in addActor where the rotation lives.
        JPH::RefConst<JPH::Shape> convertCapsule(const btCapsuleShape* shape)
        {
            const float halfHeight = shape->getHalfHeight();
            const float radius = shape->getRadius();
            JPH::CapsuleShapeSettings settings(halfHeight, radius);
            const auto result = settings.Create();
            if (result.HasError())
            {
                Log(Debug::Warning) << "Jolt CapsuleShape rejected: " << result.GetError();
                return nullptr;
            }
            return result.Get();
        }

        // Cylinder: same axis caveat as capsule. CylinderShape is
        // along Y; the Bullet variant determines the local rotation
        // when this is composed into a body.
        JPH::RefConst<JPH::Shape> convertCylinder(const btCylinderShape* shape)
        {
            const auto half = shape->getHalfExtentsWithMargin();
            JPH::CylinderShapeSettings settings(half.y(), std::max(half.x(), half.z()));
            const auto result = settings.Create();
            if (result.HasError())
            {
                Log(Debug::Warning) << "Jolt CylinderShape rejected: " << result.GetError();
                return nullptr;
            }
            return result.Get();
        }
    }

    JPH::RefConst<JPH::Shape> convertBulletShape(const btCollisionShape* shape)
    {
        if (shape == nullptr)
            return nullptr;

        const int t = shape->getShapeType();
        switch (t)
        {
            case BOX_SHAPE_PROXYTYPE:
                return convertBox(static_cast<const btBoxShape*>(shape));
            case SPHERE_SHAPE_PROXYTYPE:
                return convertSphere(static_cast<const btSphereShape*>(shape));
            case CAPSULE_SHAPE_PROXYTYPE:
                return convertCapsule(static_cast<const btCapsuleShape*>(shape));
            case CYLINDER_SHAPE_PROXYTYPE:
                return convertCylinder(static_cast<const btCylinderShape*>(shape));
            default:
                // Compound (phase 6b), triangle mesh (phase 6b),
                // height field (phase 6c) all land here for now.
                Log(Debug::Warning) << "Jolt shape converter: unsupported Bullet shape type "
                                    << t << " — collision will be missing";
                return nullptr;
        }
    }
}

#endif
