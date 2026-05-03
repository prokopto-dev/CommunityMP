#include "joltshapeconverter.hpp"

#if OPENMW_PHYSICS_USES_JOLT

#include <limits>
#include <vector>

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <BulletCollision/CollisionShapes/btConcaveShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btScaledBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <LinearMath/btQuaternion.h>
#include <LinearMath/btTransform.h>

#include <components/bullethelpers/processtrianglecallback.hpp>

#include <components/debug/debuglog.hpp>

namespace MWPhysics
{
    namespace
    {
        JPH::Vec3 toJolt(const btVector3& v)
        {
            return JPH::Vec3(v.x(), v.y(), v.z());
        }

        JPH::Quat toJolt(const btQuaternion& q)
        {
            return JPH::Quat(q.x(), q.y(), q.z(), q.w());
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

    namespace
    {
        // Triangle mesh: walk the Bullet shape's processAllTriangles
        // and append each triangle's vertices + indices into the
        // Jolt-side buffers. Bullet's processAllTriangles emits
        // triangles in arbitrary order without a vertex de-dup pass;
        // we don't dedupe either because Jolt's MeshShape internally
        // builds its own BVH and a one-time per-load duplication is
        // cheap compared to the full-cell load cost.
        //
        // Accepts both btBvhTriangleMeshShape (raw BVH mesh) and
        // btScaledBvhTriangleMeshShape (BVH mesh + local scale). The
        // scaled wrapper's processAllTriangles applies the scale per
        // triangle before invoking the callback, so the same code
        // path works for both — they share btConcaveShape as parent.
        JPH::RefConst<JPH::Shape> convertTriangleMesh(const btConcaveShape* mesh)
        {
            JPH::VertexList vertices;
            JPH::IndexedTriangleList triangles;

            auto callback = BulletHelpers::makeProcessTriangleCallback(
                [&](btVector3* tri, int /*partId*/, int /*triIndex*/) {
                    const JPH::uint32 base = static_cast<JPH::uint32>(vertices.size());
                    vertices.emplace_back(JPH::Float3(tri[0].x(), tri[0].y(), tri[0].z()));
                    vertices.emplace_back(JPH::Float3(tri[1].x(), tri[1].y(), tri[1].z()));
                    vertices.emplace_back(JPH::Float3(tri[2].x(), tri[2].y(), tri[2].z()));
                    triangles.emplace_back(base, base + 1, base + 2);
                });

            // Pass a maximally permissive AABB so the BVH visits all
            // triangles (we want the whole mesh, not a subset).
            const btScalar inf = std::numeric_limits<btScalar>::max();
            const btVector3 aabbMin(-inf, -inf, -inf);
            const btVector3 aabbMax(inf, inf, inf);
            const_cast<btConcaveShape*>(mesh)->processAllTriangles(&callback, aabbMin, aabbMax);

            if (triangles.empty())
            {
                Log(Debug::Warning) << "Jolt MeshShape: source had zero triangles";
                return nullptr;
            }

            JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
            const auto result = settings.Create();
            if (result.HasError())
            {
                Log(Debug::Warning) << "Jolt MeshShape rejected: " << result.GetError();
                return nullptr;
            }
            return result.Get();
        }

        // Compound shapes hold N child shapes each with a local
        // transform. Convert recursively. If a child fails to
        // convert (unsupported type), drop it with a warning rather
        // than failing the whole compound — partial collision is
        // better than no collision.
        JPH::RefConst<JPH::Shape> convertCompound(const btCompoundShape* compound)
        {
            JPH::StaticCompoundShapeSettings settings;
            int added = 0;
            const int n = compound->getNumChildShapes();
            for (int i = 0; i < n; ++i)
            {
                const btCollisionShape* child = compound->getChildShape(i);
                JPH::RefConst<JPH::Shape> joltChild = convertBulletShape(child);
                if (!joltChild)
                    continue;

                const btTransform& xform = compound->getChildTransform(i);
                settings.AddShape(toJolt(xform.getOrigin()), toJolt(xform.getRotation()), joltChild.GetPtr());
                ++added;
            }

            if (added == 0)
            {
                Log(Debug::Warning) << "Jolt CompoundShape empty: no convertible children of " << n;
                return nullptr;
            }

            const auto result = settings.Create();
            if (result.HasError())
            {
                Log(Debug::Warning) << "Jolt StaticCompoundShape rejected: " << result.GetError();
                return nullptr;
            }
            return result.Get();
        }
    }

    namespace
    {
        // Recursively walk a Bullet shape (handles compounds) and
        // append every triangle vertex into `out`. Used by
        // extractConvexHull to feed Jolt's ConvexHullShapeSettings.
        void collectVertices(const btCollisionShape* shape, std::vector<JPH::Vec3>& out)
        {
            if (shape == nullptr)
                return;
            const int t = shape->getShapeType();
            if (t == COMPOUND_SHAPE_PROXYTYPE)
            {
                const auto* compound = static_cast<const btCompoundShape*>(shape);
                const int n = compound->getNumChildShapes();
                for (int i = 0; i < n; ++i)
                {
                    const btTransform& xform = compound->getChildTransform(i);
                    const size_t before = out.size();
                    collectVertices(compound->getChildShape(i), out);
                    // Apply child transform to its vertices.
                    for (size_t v = before; v < out.size(); ++v)
                    {
                        const btVector3 lp(out[v].GetX(), out[v].GetY(), out[v].GetZ());
                        const btVector3 wp = xform * lp;
                        out[v] = JPH::Vec3(wp.x(), wp.y(), wp.z());
                    }
                }
                return;
            }
            if (shape->isConcave())
            {
                auto callback = BulletHelpers::makeProcessTriangleCallback(
                    [&](btVector3* tri, int /*partId*/, int /*triIndex*/) {
                        out.emplace_back(tri[0].x(), tri[0].y(), tri[0].z());
                        out.emplace_back(tri[1].x(), tri[1].y(), tri[1].z());
                        out.emplace_back(tri[2].x(), tri[2].y(), tri[2].z());
                    });
                const btScalar inf = std::numeric_limits<btScalar>::max();
                const btVector3 aabbMin(-inf, -inf, -inf);
                const btVector3 aabbMax(inf, inf, inf);
                const_cast<btConcaveShape*>(static_cast<const btConcaveShape*>(shape))
                    ->processAllTriangles(&callback, aabbMin, aabbMax);
                return;
            }
            // Convex primitives: walk their AABB corners. Cheap and
            // generates a viable hull for boxes, spheres, cylinders.
            btVector3 aabbMin, aabbMax;
            btTransform identity;
            identity.setIdentity();
            shape->getAabb(identity, aabbMin, aabbMax);
            for (int i = 0; i < 8; ++i)
            {
                out.emplace_back((i & 1) ? aabbMax.x() : aabbMin.x(),
                    (i & 2) ? aabbMax.y() : aabbMin.y(),
                    (i & 4) ? aabbMax.z() : aabbMin.z());
            }
        }
    }

    JPH::RefConst<JPH::Shape> extractConvexHull(const btCollisionShape* shape)
    {
        if (shape == nullptr)
            return nullptr;
        std::vector<JPH::Vec3> vertices;
        collectVertices(shape, vertices);
        if (vertices.size() < 4)
        {
            Log(Debug::Warning) << "Jolt ConvexHullShape: source had only " << vertices.size()
                                << " vertices (< 4)";
            return nullptr;
        }
        JPH::Array<JPH::Vec3> jphVerts;
        jphVerts.reserve(vertices.size());
        for (const JPH::Vec3& v : vertices)
            jphVerts.push_back(v);
        JPH::ConvexHullShapeSettings settings(std::move(jphVerts));
        const auto result = settings.Create();
        if (result.HasError())
        {
            Log(Debug::Warning) << "Jolt ConvexHullShape rejected: " << result.GetError();
            return nullptr;
        }
        return result.Get();
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
            case COMPOUND_SHAPE_PROXYTYPE:
                return convertCompound(static_cast<const btCompoundShape*>(shape));
            case TRIANGLE_MESH_SHAPE_PROXYTYPE:
            case SCALED_TRIANGLE_MESH_SHAPE_PROXYTYPE:
                return convertTriangleMesh(static_cast<const btConcaveShape*>(shape));
            default:
                // Height field (phase 6c) lands here for now.
                Log(Debug::Warning) << "Jolt shape converter: unsupported Bullet shape type "
                                    << t << " — collision will be missing";
                return nullptr;
        }
    }
}

#endif
