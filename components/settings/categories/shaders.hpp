#ifndef OPENMW_COMPONENTS_SETTINGS_CATEGORIES_SHADERS_H
#define OPENMW_COMPONENTS_SETTINGS_CATEGORIES_SHADERS_H

#include <components/sceneutil/lightingmethod.hpp>
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
    struct ShadersCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<bool> mForcePerPixelLighting{ mIndex, "Shaders", "force per pixel lighting" };
        SettingValue<bool> mClampLighting{ mIndex, "Shaders", "clamp lighting" };
        SettingValue<bool> mAutoUseObjectNormalMaps{ mIndex, "Shaders", "auto use object normal maps" };
        SettingValue<bool> mAutoUseObjectSpecularMaps{ mIndex, "Shaders", "auto use object specular maps" };
        SettingValue<bool> mAutoUseTerrainNormalMaps{ mIndex, "Shaders", "auto use terrain normal maps" };
        SettingValue<bool> mAutoUseTerrainSpecularMaps{ mIndex, "Shaders", "auto use terrain specular maps" };
        SettingValue<std::string> mNormalMapPattern{ mIndex, "Shaders", "normal map pattern" };
        SettingValue<std::string> mNormalHeightMapPattern{ mIndex, "Shaders", "normal height map pattern" };
        SettingValue<std::string> mSpecularMapPattern{ mIndex, "Shaders", "specular map pattern" };
        SettingValue<std::string> mTerrainSpecularMapPattern{ mIndex, "Shaders", "terrain specular map pattern" };
        SettingValue<bool> mApplyLightingToEnvironmentMaps{ mIndex, "Shaders", "apply lighting to environment maps" };
        SettingValue<SceneUtil::LightingMethod> mLightingMethod{ mIndex, "Shaders", "lighting method" };
        SettingValue<bool> mClassicFalloff{ mIndex, "Shaders", "classic falloff" };
        SettingValue<bool> mMatchSunlightToSun{ mIndex, "Shaders", "match sunlight to sun" };
        SettingValue<float> mLightBoundsMultiplier{ mIndex, "Shaders", "light bounds multiplier",
            makeClampSanitizerFloat(0, 5) };
        SettingValue<float> mMaximumLightDistance{ mIndex, "Shaders", "maximum light distance",
            makeMaxSanitizerFloat(0) };
        SettingValue<float> mLightFadeStart{ mIndex, "Shaders", "light fade start", makeClampSanitizerFloat(0, 1) };
        SettingValue<int> mMaxLights{ mIndex, "Shaders", "max lights", makeClampSanitizerInt(2, 64) };
        SettingValue<float> mMinimumInteriorBrightness{ mIndex, "Shaders", "minimum interior brightness",
            makeClampSanitizerFloat(0, 1) };
        SettingValue<bool> mAntialiasAlphaTest{ mIndex, "Shaders", "antialias alpha test" };
        SettingValue<bool> mAdjustCoverageForAlphaTest{ mIndex, "Shaders", "adjust coverage for alpha test" };
        SettingValue<bool> mSoftParticles{ mIndex, "Shaders", "soft particles" };
        SettingValue<bool> mWeatherParticleOcclusion{ mIndex, "Shaders", "weather particle occlusion" };
        SettingValue<float> mWeatherParticleOcclusionSmallFeatureCullingPixelSize{ mIndex, "Shaders",
            "weather particle occlusion small feature culling pixel size" };
        // Parallax mapping depth (offset multiplier per height-map sample).
        // Pushed as a global uniform; affects every shader that includes
        // lib/material/parallax.glsl. Higher = deeper relief, but past ~0.1
        // visible artefacts appear at glancing angles.
        SettingValue<float> mParallaxScale{ mIndex, "Shaders", "parallax scale",
            makeClampSanitizerFloat(0.0f, 0.5f) };
        SettingValue<float> mParallaxBias{ mIndex, "Shaders", "parallax bias",
            makeClampSanitizerFloat(-0.25f, 0.25f) };
        // Per-texture parallax scale overrides. Format: a semicolon-separated
        // list of `pattern=scale` pairs. The pattern is matched as a
        // case-insensitive substring against the diffuse map's filename.
        // Example:
        //   parallax overrides = Tx_imp_wall_01=0.08;Tx_BC_grass=0.02
        // The first matching pattern wins; non-matching meshes use the
        // global `parallax scale` value.
        SettingValue<std::string> mParallaxOverrides{ mIndex, "Shaders", "parallax overrides" };

        // Grass wind: animated vertex displacement on grass-coloured terrain
        // pixels (vertex.color.g dominant) to fake a wind-blown lawn.
        SettingValue<bool> mGrassWind{ mIndex, "Shaders", "grass wind" };
        SettingValue<float> mGrassWindAmplitude{ mIndex, "Shaders", "grass wind amplitude",
            makeClampSanitizerFloat(0.0f, 64.0f) };
        SettingValue<float> mGrassWindSpeed{ mIndex, "Shaders", "grass wind speed",
            makeClampSanitizerFloat(0.0f, 8.0f) };
        SettingValue<float> mGrassWindFrequency{ mIndex, "Shaders", "grass wind frequency",
            makeClampSanitizerFloat(0.0f, 0.5f) };
        SettingValue<osg::Vec2f> mGrassWindDir{ mIndex, "Shaders", "grass wind dir" };

        // GGX/Cook-Torrance specular instead of Phong. Roughness derived
        // from material shininess (legacy meshes); a future pass can read
        // a dedicated _s.dds smoothness map. Pushed via @pbrSpecular global
        // define — no per-mesh setup required.
        SettingValue<bool> mPbrSpecular{ mIndex, "Shaders", "pbr specular" };
    };
}

#endif
