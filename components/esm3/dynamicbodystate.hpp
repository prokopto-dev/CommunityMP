#ifndef OPENMW_COMPONENTS_ESM3_DYNAMICBODYSTATE_H
#define OPENMW_COMPONENTS_ESM3_DYNAMICBODYSTATE_H

#include <cstdint>

#include <components/misc/concepts.hpp>

namespace ESM
{
    // Per-ref dynamic-body promotion record (Phase 6c of
    // docs/imgui-overlay-plan.md). Written into ObjectState only when
    // the ref has been promoted via the ImGui spawner / inspector so
    // the same ref returns as a Jolt rigid body after save/load.
    // Quat stored explicitly (xyzw) so we can skip the Euler
    // round-trip — RefData::mPosition.rot is read-only as far as the
    // dynamic path is concerned.
    struct DynamicBodyState
    {
        uint8_t mShape = 0; // mirrors IPhysicsBackend::DynamicShape
        uint8_t mPadding[3] = {};
        float mHalfExtents[3] = { 0.0f, 0.0f, 0.0f };
        float mMass = 0.0f;
        float mRotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // (x, y, z, w)
    };

    template <Misc::SameAsWithoutCvref<DynamicBodyState> T>
    void decompose(T&& v, const auto& f)
    {
        f(v.mShape, v.mPadding, v.mHalfExtents, v.mMass, v.mRotation);
    }
}

#endif
