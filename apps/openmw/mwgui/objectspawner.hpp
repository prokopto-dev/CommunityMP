#ifndef MWGUI_OBJECTSPAWNER_H
#define MWGUI_OBJECTSPAWNER_H

#include <string>

#include <components/esm/refid.hpp>

namespace MWGui
{
    // Phase 5 of docs/imgui-overlay-plan.md: pick a record from the
    // ESM store and drop it in the world. Mirrors the console
    // `PlaceAtMe` path (mwscript/transformationextensions.cpp) so
    // anything spawned this way persists to saves the same way.
    class ObjectSpawner
    {
    public:
        void draw();

    private:
        enum class Type
        {
            Static,
            Activator,
            Container,
            Light,
            Misc,
            Door,
        };
        enum class PlacementMode
        {
            AtPlayer,
            AtCrosshair,
            AtCoords,
        };
        enum class DynamicShape
        {
            Box,
            Cylinder,
            Sphere,
            Mesh,
        };

        Type mType = Type::Static;
        PlacementMode mPlacement = PlacementMode::AtPlayer;
        char mSearchFilter[128] = {};
        ESM::RefId mSelected;
        float mCoords[3] = { 0.0f, 0.0f, 0.0f };
        float mZRotDeg = 0.0f;
        int mCount = 1;
        std::string mLastResult;

        // Phase 6 — dynamic Jolt body promotion.
        bool mDynamic = false;
        DynamicShape mDynamicShape = DynamicShape::Cylinder;
        float mDynamicHalfExtents[3] = { 32.0f, 32.0f, 48.0f };
        float mDynamicMass = 25.0f;

        void renderCatalog();
        void doSpawn();
    };
}

#endif
