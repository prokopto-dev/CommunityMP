#ifndef OPENMW_COMPONENTS_SETTINGS_CATEGORIES_RAYTRACING_H
#define OPENMW_COMPONENTS_SETTINGS_CATEGORIES_RAYTRACING_H

#include <components/settings/sanitizerimpl.hpp>
#include <components/settings/settingvalue.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace Settings
{
    struct RaytracingCategory : WithIndex
    {
        using WithIndex::WithIndex;

        // Screen-space contact shadows raymarched along the sun direction.
        // Augments the cascaded shadow maps; eliminates peter-panning at
        // contact points. Cheap (~1ms at 1080p), works on any GL 2.1+ context.
        SettingValue<bool> mContactShadows{ mIndex, "Raytracing", "contact shadows" };
        SettingValue<int> mContactShadowsSteps{ mIndex, "Raytracing", "contact shadows steps",
            makeClampSanitizerInt(4, 64) };
        SettingValue<float> mContactShadowsLength{ mIndex, "Raytracing", "contact shadows length",
            makeClampSanitizerFloat(0.f, 1.f) };
        SettingValue<float> mContactShadowsThickness{ mIndex, "Raytracing", "contact shadows thickness",
            makeClampSanitizerFloat(0.f, 1.f) };

        // Ground-Truth Ambient Occlusion (raymarched in screen space).
        // Multiplies the ambient term and adds visual depth to interiors.
        SettingValue<bool> mSsao{ mIndex, "Raytracing", "ssao" };
        SettingValue<float> mSsaoRadius{ mIndex, "Raytracing", "ssao radius",
            makeClampSanitizerFloat(0.05f, 4096.f) };
        SettingValue<float> mSsaoStrength{ mIndex, "Raytracing", "ssao strength",
            makeClampSanitizerFloat(0.f, 4.f) };
        SettingValue<int> mSsaoSamples{ mIndex, "Raytracing", "ssao samples",
            makeClampSanitizerInt(2, 32) };
    };
}

#endif
