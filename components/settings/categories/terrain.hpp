#ifndef OPENMW_COMPONENTS_SETTINGS_CATEGORIES_TERRAIN_H
#define OPENMW_COMPONENTS_SETTINGS_CATEGORIES_TERRAIN_H

#include <components/settings/sanitizerimpl.hpp>
#include <components/settings/settingvalue.hpp>

#include <osg/Math>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <cstdint>
#include <string>
#include <string_view>

namespace Settings
{
    struct TerrainCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<bool> mDistantTerrain{ mIndex, "Terrain", "distant terrain" };
        SettingValue<float> mLodFactor{ mIndex, "Terrain", "lod factor", makeMaxStrictSanitizerFloat(0) };
        SettingValue<int> mVertexLodMod{ mIndex, "Terrain", "vertex lod mod" };
        SettingValue<int> mCompositeMapLevel{ mIndex, "Terrain", "composite map level", makeMaxSanitizerInt(-3) };
        SettingValue<int> mCompositeMapResolution{ mIndex, "Terrain", "composite map resolution",
            makeMaxSanitizerInt(1) };
        SettingValue<float> mMaxCompositeGeometrySize{ mIndex, "Terrain", "max composite geometry size",
            makeMaxSanitizerFloat(1) };
        SettingValue<bool> mDebugChunks{ mIndex, "Terrain", "debug chunks" };
        SettingValue<bool> mObjectPaging{ mIndex, "Terrain", "object paging" };
        SettingValue<bool> mObjectPagingActiveGrid{ mIndex, "Terrain", "object paging active grid" };
        SettingValue<float> mObjectPagingMergeFactor{ mIndex, "Terrain", "object paging merge factor",
            makeMaxStrictSanitizerFloat(0) };
        SettingValue<float> mObjectPagingMinSize{ mIndex, "Terrain", "object paging min size",
            makeMaxStrictSanitizerFloat(0) };
        SettingValue<float> mObjectPagingMinSizeMergeFactor{ mIndex, "Terrain", "object paging min size merge factor",
            makeMaxStrictSanitizerFloat(0) };
        SettingValue<float> mObjectPagingMinSizeCostMultiplier{ mIndex, "Terrain",
            "object paging min size cost multiplier", makeMaxStrictSanitizerFloat(0) };
        SettingValue<bool> mWaterCulling{ mIndex, "Terrain", "water culling" };

        SettingValue<bool> mTessellation{ mIndex, "Terrain", "tessellation" };
        SettingValue<int> mTessellationMaxLevel{ mIndex, "Terrain", "tessellation max level",
            makeClampSanitizerInt(1, 64) };
        SettingValue<float> mTessellationDisplacementScale{ mIndex, "Terrain", "tessellation displacement scale",
            makeClampSanitizerFloat(0.f, 256.f) };
        SettingValue<float> mTessellationViewDistance{ mIndex, "Terrain", "tessellation view distance",
            makeMaxStrictSanitizerFloat(0) };
        // Software fallback for the hardware tessellation feature: applies
        // procedural FBM displacement in the (legacy GL 1.20-compatible)
        // terrain vertex shader. Works without a GL 4.0 context, intended for
        // macOS native (GL 2.1) where hardware tessellation is unreachable.
        SettingValue<bool> mTessellationEmulation{ mIndex, "Terrain", "tessellation emulation" };
        // CPU-side mesh densification factor for the emulation path. Each
        // near-camera chunk's vertex grid is interpolated to factor * (n-1)+1
        // resolution before upload. Restricted to powers of two (1, 2, 4) so
        // the stitching at chunk borders remains valid. Higher factors quickly
        // increase memory and per-chunk build cost; 2 is the practical sweet
        // spot.
        SettingValue<int> mTessellationEmulationFactor{ mIndex, "Terrain", "tessellation emulation factor",
            makeClampSanitizerInt(1, 4) };

        // Procedurally-generated bump heightmap (FBM, 256x256, baked once at
        // boot) used to perturb the terrain normal in the fragment shader.
        // Adds visible micro-relief to grass/dirt at no geometry cost; works
        // independently of tessellation. Effect strength scales the
        // perturbation; UV scale controls feature frequency.
        SettingValue<bool> mProceduralBump{ mIndex, "Terrain", "procedural bump" };
        SettingValue<float> mProceduralBumpStrength{ mIndex, "Terrain", "procedural bump strength",
            makeClampSanitizerFloat(0.f, 4.f) };
        SettingValue<float> mProceduralBumpScale{ mIndex, "Terrain", "procedural bump scale",
            makeClampSanitizerFloat(0.0001f, 1.f) };
    };
}

#endif
