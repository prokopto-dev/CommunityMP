#version 120

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

#include "lib/core/fragment.h.glsl"

// Inspired by Blender GLSL Water by martinsh ( https://devlog-martinsh.blogspot.de/2012/07/waterundewater-shader-wip.html )

// tweakables -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

const float VISIBILITY = 2500.0;
const float VISIBILITY_DEPTH = VISIBILITY * 1.5;
const float DEPTH_FADE = 0.15;

const vec2 BIG_WAVES = vec2(0.18, 0.18);   // boosted: visible swell rolling across the water
const vec2 MID_WAVES = vec2(0.20, 0.20);   // boosted: mid-frequency wave pattern
const vec2 MID_WAVES_RAIN = vec2(0.35, 0.35);
const vec2 SMALL_WAVES = vec2(0.28, 0.28); // boosted: more visible texture
const vec2 SMALL_WAVES_RAIN = vec2(0.50, 0.50);
const vec2 TINY_WAVES = vec2(0.22, 0.22);  // high-frequency wind ripples
const vec2 MICRO_WAVES = vec2(0.10, 0.10); // micro-glint frequency

const float WAVE_CHOPPYNESS = 0.12;                // sharper crests, Gerstner-like
const float WAVE_SCALE = 75.0;                     // overall wave scale

const float BUMP = 0.30;                           // boosted: visible waves while staying reflective
const float BUMP_RAIN = 1.4;                       // rainy weather chops it up significantly
const float REFL_BUMP = 0.05;                      // reflection slightly distorted (still readable)
const float REFR_BUMP = 0.04;                      // refraction barely distorted

#if @sunlightScattering
const float SCATTER_AMOUNT = 0.3;                  // amount of sunlight scattering
const vec3 SCATTER_COLOUR = vec3(0.0,1.0,0.95);    // colour of sunlight scattering
const vec3 SUN_EXT = vec3(0.45, 0.55, 0.68);       // sunlight extinction
#endif

const float SUN_SPEC_FADING_THRESHOLD = 0.15;       // visibility at which sun specularity starts to fade
const float SPEC_HARDNESS = 768.0;                 // (was 256) much sharper hot spot
const float SPEC_BUMPINESS = 7.5;                  // (was 5) more wave bumps drive the glint
const float SPEC_BRIGHTNESS = 4.0;                 // (was 1.5) brighter glints, near-pure white
const float SUN_GLINT_AMPLITUDE = 1.5;             // multiplies the glint contribution
const float SUN_GLINT_STIPPLE = 0.35;              // size of the sparkle stipples (hash-driven)

const float BUMP_SUPPRESS_DEPTH = 300.0;           // at what water depth bumpmap will be suppressed for reflections and refractions (prevents artifacts at shores)
const float REFR_FOG_DISTORT_DISTANCE = 3000.0;    // at what distance refraction fog will be calculated using real water depth instead of distorted depth (prevents splotchy shores)

const vec2 WIND_DIR = vec2(0.5f, -0.8f);
const float WIND_SPEED = 0.2f;

const vec3 WATER_COLOR = vec3(0.090195, 0.115685, 0.12745);

#if @wobblyShores
const float WOBBLY_SHORE_FADE_DISTANCE = 6200.0;   // fade out wobbly shores to mask precision errors, the effect is almost impossible to see at a distance
#endif

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -

vec2 normalCoords(vec2 uv, float scale, float speed, float time, float timer1, float timer2, vec3 previousNormal)
{
  return uv * (WAVE_SCALE * scale) + WIND_DIR * time * (WIND_SPEED * speed) -(previousNormal.xy/previousNormal.zz) * WAVE_CHOPPYNESS + vec2(time * timer1,time * timer2);
}

uniform sampler2D rippleMap;
uniform vec3 playerPos;

varying vec3 worldPos;

varying vec2 rippleMapUV;

varying vec4 position;
varying float linearDepth;

uniform sampler2D normalMap;

uniform float osg_SimulationTime;

uniform float near;
uniform float far;

uniform float rainIntensity;

uniform vec2 screenRes;

#define PER_PIXEL_LIGHTING 0

#include "shadows_fragment.glsl"
#include "lib/light/lighting.glsl"
#include "fog.glsl"
#include "lib/water/fresnel.glsl"
#include "lib/water/rain_ripples.glsl"
#include "lib/view/depth.glsl"

void main(void)
{
    vec2 UV = worldPos.xy / (8192.0*5.0) * 3.0;

    float shadow = unshadowedLightRatio(linearDepth);

    vec2 screenCoords = gl_FragCoord.xy / screenRes;

    #define waterTimer osg_SimulationTime

    vec3 normal0 = 2.0 * texture2D(normalMap,normalCoords(UV, 0.05, 0.04, waterTimer, -0.015, -0.005, vec3(0.0,0.0,0.0))).rgb - 1.0;
    vec3 normal1 = 2.0 * texture2D(normalMap,normalCoords(UV, 0.1,  0.08, waterTimer,  0.02,   0.015, normal0)).rgb - 1.0;
    vec3 normal2 = 2.0 * texture2D(normalMap,normalCoords(UV, 0.25, 0.07, waterTimer, -0.04,  -0.03,  normal1)).rgb - 1.0;
    vec3 normal3 = 2.0 * texture2D(normalMap,normalCoords(UV, 0.5,  0.09, waterTimer,  0.03,   0.04,  normal2)).rgb - 1.0;
    vec3 normal4 = 2.0 * texture2D(normalMap,normalCoords(UV, 1.0,  0.4,  waterTimer, -0.02,   0.1,   normal3)).rgb - 1.0;
    vec3 normal5 = 2.0 * texture2D(normalMap,normalCoords(UV, 2.0,  0.7,  waterTimer,  0.1,   -0.06,  normal4)).rgb - 1.0;
    // Two new high-frequency octaves: tiny wind ripples (~scale 4) and
    // micro-glint chop (~scale 8). Move faster than the larger waves so
    // the surface always looks "alive" even when the camera is still.
    vec3 normal6 = 2.0 * texture2D(normalMap,normalCoords(UV, 4.0,  1.6,  waterTimer, -0.18,   0.22, normal5)).rgb - 1.0;
    vec3 normal7 = 2.0 * texture2D(normalMap,normalCoords(UV, 8.0,  3.2,  waterTimer,  0.31,  -0.28, normal6)).rgb - 1.0;

    vec4 rainRipple;

    if (rainIntensity > 0.01)
        rainRipple = rainCombined(position.xy/1000.0, waterTimer) * clamp(rainIntensity, 0.0, 1.0);
    else
        rainRipple = vec4(0.0);

    vec3 rippleAdd = rainRipple.xyz * 10.0;

    float distToCenter = length(rippleMapUV - vec2(0.5));
    float blendClose = smoothstep(0.001, 0.02, distToCenter);
    float blendFar = 1.0 - smoothstep(0.3, 0.4, distToCenter);
    // Player/physics interaction ripples — boosted so the player wading
    // through water leaves a clearly visible wake.
    float distortionLevel = 5.0;
    rippleAdd += distortionLevel * vec3(texture2D(rippleMap, rippleMapUV).ba * blendFar * blendClose, 0.0);

    vec2 bigWaves = BIG_WAVES;
    vec2 midWaves = mix(MID_WAVES, MID_WAVES_RAIN, rainIntensity);
    vec2 smallWaves = mix(SMALL_WAVES, SMALL_WAVES_RAIN, rainIntensity);
    float bump = mix(BUMP,BUMP_RAIN,rainIntensity);

    vec3 normal = (normal0 * bigWaves.x + normal1 * bigWaves.y + normal2 * midWaves.x +
                   normal3 * midWaves.y + normal4 * smallWaves.x + normal5 * smallWaves.y +
                   normal6 * TINY_WAVES.x + normal7 * MICRO_WAVES.x + rippleAdd);
    normal = normalize(vec3(-normal.x * bump, -normal.y * bump, normal.z));

    vec3 sunWorldDir = normalize((gl_ModelViewMatrixInverse * vec4(lcalcPosition(0).xyz, 0.0)).xyz);
    vec3 cameraPos = (gl_ModelViewMatrixInverse * vec4(0,0,0,1)).xyz;
    vec3 viewDir = normalize(position.xyz - cameraPos.xyz);

    float sunFade = length(gl_LightModel.ambient.xyz);

    // fresnel
    float ior = (cameraPos.z>0.0)?(1.333/1.0):(1.0/1.333); // air to water; water to air
    float fresnel = clamp(fresnel_dielectric(viewDir, normal, ior), 0.0, 1.0);
    // Reflection bias: lift the fresnel curve so the water is clearly
    // reflective at all angles, but not a chrome mirror. Pure physical
    // fresnel for water at normal incidence would be ~2% — too low to
    // feel reflective. We split the difference at 45%.
    fresnel = mix(0.45, 1.0, fresnel);

    vec2 screenCoordsOffset = normal.xy * REFL_BUMP;
#if @waterRefraction
    float depthSample = linearizeDepth(sampleRefractionDepthMap(screenCoords), near, far);
    float surfaceDepth = linearizeDepth(gl_FragCoord.z, near, far);
    float realWaterDepth = depthSample - surfaceDepth;  // undistorted water depth in view direction, independent of frustum
    float depthSampleDistorted = linearizeDepth(sampleRefractionDepthMap(screenCoords - screenCoordsOffset), near, far);
    float waterDepthDistorted = max(depthSampleDistorted - surfaceDepth, 0.0);
    screenCoordsOffset *= clamp(realWaterDepth / BUMP_SUPPRESS_DEPTH, 0.0, 1.0);
#endif
    // -- Screen-space reflection raycast --
    //
    // The plane reflection map captures the static scene mirrored above the
    // water plane. It misses anything occluded by glancing geometry near the
    // surface (boat hulls, dock pillars, NPCs at shore). We complement it
    // with a short SSR raycast against the existing refractionDepthMap (which
    // is in fact the full opaque depth of the scene). When the ray hits a
    // surface, we sample the reflection map at the projected hit UV, giving
    // a much more accurate near-camera reflection.
    vec3 reflection;
    {
        // Reflection direction in view space.
        vec3 viewN_vs = normalize(gl_NormalMatrix * normal);
        vec3 viewPos_vs = (gl_ModelViewMatrix * position).xyz;
        vec3 viewDir_vs = normalize(viewPos_vs);
        vec3 reflDir_vs = reflect(viewDir_vs, viewN_vs);

        // Bias the ray start slightly along the reflection direction so we
        // don't self-intersect at the water surface.
        vec3 rayOrigin_vs = viewPos_vs + reflDir_vs * 1.0;

        const int SSR_STEPS = 14;
        const float SSR_MAX_LENGTH = 5000.0; // world units, capped
        float stepLen = SSR_MAX_LENGTH / float(SSR_STEPS);

        // Bayer 4x4 dither to break banding.
        // GLSL 120 has no bitwise '&' on ints; use mod() instead.
        // We also avoid float[16] array literals (also a 130+ thing) by
        // unrolling a small lookup with mix().
        float fxm = mod(gl_FragCoord.x, 4.0);
        float fym = mod(gl_FragCoord.y, 4.0);
        // 4x4 Bayer pattern, computed in-line as a 2D function.
        // Source values in [0..15], normalised to [0..1).
        float row0 = mix(mix(0.0,  8.0, step(0.5, fxm)),
                         mix(2.0, 10.0, step(2.5, fxm)),
                         step(1.5, fxm));
        float row1 = mix(mix(12.0, 4.0, step(0.5, fxm)),
                         mix(14.0, 6.0, step(2.5, fxm)),
                         step(1.5, fxm));
        float row2 = mix(mix(3.0, 11.0, step(0.5, fxm)),
                         mix(1.0,  9.0, step(2.5, fxm)),
                         step(1.5, fxm));
        float row3 = mix(mix(15.0, 7.0, step(0.5, fxm)),
                         mix(13.0, 5.0, step(2.5, fxm)),
                         step(1.5, fxm));
        float dither = mix(mix(row0, row1, step(0.5, fym)),
                           mix(row2, row3, step(2.5, fym)),
                           step(1.5, fym)) / 16.0;

        vec2 ssrHitUV = vec2(-1.0);
        for (int i = 1; i <= SSR_STEPS; ++i)
        {
            float t = (float(i) + dither) * stepLen;
            vec3 sampleVS = rayOrigin_vs + reflDir_vs * t;

            vec4 sampleClip = gl_ProjectionMatrix * vec4(sampleVS, 1.0);
            if (sampleClip.w <= 0.0) break;
            vec2 sUV = (sampleClip.xy / sampleClip.w) * 0.5 + 0.5;
            if (sUV.x < 0.0 || sUV.x > 1.0 || sUV.y < 0.0 || sUV.y > 1.0) break;

            float scnDepth = linearizeDepth(sampleRefractionDepthMap(sUV), near, far);
            // sampleVS.z is negative (forward in view space). distance to camera.
            float rayDepth = -sampleVS.z;
            float diff = scnDepth - rayDepth;
            if (diff > 0.0 && diff < stepLen * 1.5)
            {
                ssrHitUV = sUV;
                break;
            }
        }

        vec2 planeUV = screenCoords + screenCoordsOffset;
        vec3 planeRefl = sampleReflectionMap(planeUV).rgb;
        if (ssrHitUV.x >= 0.0)
        {
            // Mix the SSR hit (more locally correct) with the plane reflection
            // (gives sky/distant scene). Bias toward SSR as the hit gets
            // closer to the screen edges to fade out near boundaries.
            vec2 edgeFade = vec2(0.5) - abs(ssrHitUV - vec2(0.5));
            float edgeMask = clamp(min(edgeFade.x, edgeFade.y) * 6.0, 0.0, 1.0);
            vec3 ssrColor = sampleReflectionMap(ssrHitUV).rgb;
            reflection = mix(planeRefl, ssrColor, edgeMask * 0.55);
        }
        else
        {
            reflection = planeRefl;
        }
    }

    vec3 waterColor = WATER_COLOR * sunFade;

    vec4 sunSpec = lcalcSpecular(0);
    // alpha component is sun visibility; we want to start fading lighting effects when visibility is low
    sunSpec.a = min(1.0, sunSpec.a / SUN_SPEC_FADING_THRESHOLD);

    // specular
    const float SPEC_MAGIC = 1.55; // from the original blender shader, changing it makes the spec vanish or become too bright

    // Specular normal gets full contribution of the high-frequency octaves
    // so the sun glints sparkle on micro-chop even while the reflection
    // path stays mirror-flat (governed by 'normal' / 'bump').
    vec3 specBase = normal0 * 0.05 + normal2 * 0.10 + normal4 * 0.20
                  + normal5 * 0.20 + normal6 * 0.50 + normal7 * 0.50;
    vec3 specNormal = normalize(vec3(-specBase.x * SPEC_BUMPINESS, -specBase.y * SPEC_BUMPINESS, specBase.z));
    vec3 viewReflectDir = reflect(viewDir, specNormal);
    float phongTerm = max(dot(viewReflectDir, sunWorldDir), 0.0);
    float specular = pow(atan(phongTerm * SPEC_MAGIC), SPEC_HARDNESS) * SPEC_BRIGHTNESS;

    // Sun glint stipple: a high-frequency hash modulates a second pow lobe
    // so the highlight breaks into discrete sparkles instead of one smooth
    // glow — gives a dancing-on-the-water feel.
    {
        vec2 sparkleUV = worldPos.xy * SUN_GLINT_STIPPLE + waterTimer * 0.3;
        float sparkle = fract(sin(dot(floor(sparkleUV * 32.0), vec2(127.1, 311.7))) * 43758.5453);
        sparkle = smoothstep(0.55, 1.0, sparkle);
        float glintLobe = pow(phongTerm, SPEC_HARDNESS * 0.25) * sparkle * SUN_GLINT_AMPLITUDE;
        specular += glintLobe;
    }

    specular = clamp(specular, 0.0, 4.0) * shadow * sunSpec.a;

    // artificial specularity to make rain ripples more noticeable
    vec3 skyColorEstimate = vec3(max(0.0, mix(-0.3, 1.0, sunFade)));
    vec3 rainSpecular = abs(rainRipple.w)*mix(skyColorEstimate, vec3(1.0), 0.05)*0.5;
    float waterTransparency = clamp(fresnel * 6.0 + specular, 0.0, 1.0);

#if @waterRefraction
    // selectively nullify screenCoordsOffset to eliminate remaining shore artifacts, not needed for reflection
    if (cameraPos.z > 0.0 && realWaterDepth <= VISIBILITY_DEPTH && waterDepthDistorted > VISIBILITY_DEPTH)
        screenCoordsOffset = vec2(0.0);

    depthSampleDistorted = linearizeDepth(sampleRefractionDepthMap(screenCoords - screenCoordsOffset), near, far);
    waterDepthDistorted = max(depthSampleDistorted - surfaceDepth, 0.0);

    // fade to realWaterDepth at a distance to compensate for physically inaccurate depth calculation
    waterDepthDistorted = mix(waterDepthDistorted, realWaterDepth, min(surfaceDepth / REFR_FOG_DISTORT_DISTANCE, 1.0));

    // refraction
    vec3 refraction = sampleRefractionMap(screenCoords - screenCoordsOffset).rgb;
    vec3 rawRefraction = refraction;

    // brighten up the refraction underwater
    if (cameraPos.z < 0.0)
        refraction = clamp(refraction * 1.5, 0.0, 1.0);
    else
    {
        float depthCorrection = sqrt(1.0 + 4.0 * DEPTH_FADE * DEPTH_FADE);
        float factor = DEPTH_FADE * DEPTH_FADE / (-0.5 * depthCorrection + 0.5 - waterDepthDistorted / VISIBILITY) + 0.5 * depthCorrection + 0.5;
        refraction = mix(refraction, waterColor, clamp(factor, 0.0, 1.0));
    }

#if @sunlightScattering
    vec3 scatterNormal = (normal0 * bigWaves.x * 0.5 + normal1 * bigWaves.y * 0.5 + normal2 * midWaves.x * 0.2 +
                          normal3 * midWaves.y * 0.2 + normal4 * smallWaves.x * 0.1 + normal5 * smallWaves.y * 0.1 + rippleAdd);
    scatterNormal = normalize(vec3(-scatterNormal.xy * bump, scatterNormal.z));
    float sunHeight = sunWorldDir.z;
    vec3 scatterColour = mix(SCATTER_COLOUR * vec3(1.0, 0.4, 0.0), SCATTER_COLOUR, max(1.0 - exp(-sunHeight * SUN_EXT), 0.0));
    float scatterLambert = max(dot(sunWorldDir, scatterNormal) * 0.7 + 0.3, 0.0);
    float scatterReflectAngle = max(dot(reflect(sunWorldDir, scatterNormal), viewDir) * 2.0 - 1.2, 0.0);
    float lightScatter = scatterLambert * scatterReflectAngle * SCATTER_AMOUNT * sunFade * sunSpec.a * max(1.0 - exp(-sunHeight), 0.0);
    refraction = mix(refraction, scatterColour, lightScatter);
#endif

    gl_FragData[0].rgb = mix(refraction, reflection, fresnel);
    gl_FragData[0].a = 1.0;
    // no alpha here, so make sure raindrop ripple specularity gets properly subdued
    rainSpecular *= waterTransparency;
#else
    gl_FragData[0].rgb = mix(waterColor, reflection, (1.0 + fresnel) * 0.5);
    gl_FragData[0].a = waterTransparency;
#endif

    gl_FragData[0].rgb += specular * sunSpec.rgb + rainSpecular;

#if @waterRefraction
    // -- Edge foam --
    // Detect a thin band where realWaterDepth is small (we're close to a
    // shore or submerged geometry) and add a wave-modulated bright tint.
    // Vanishes in deep water and far away.
    {
        const float FOAM_DEPTH = 90.0;
        const float FOAM_FADE_DIST = 8000.0;
        float depthFoam = clamp(1.0 - realWaterDepth / FOAM_DEPTH, 0.0, 1.0);
        depthFoam = pow(depthFoam, 1.6);
        // Animated wave mask so foam isn't a static ring.
        float waveMask = clamp(0.5 + 0.5 * (normal2.r + normal4.r), 0.0, 1.0);
        float distFade = clamp(1.0 - linearDepth / FOAM_FADE_DIST, 0.0, 1.0);
        float foam = depthFoam * waveMask * distFade;
        // Foam crests: secondary stipple so the shore foam looks like
        // breaking white-caps rather than a flat tinted band.
        vec2 foamUV = worldPos.xy * 0.06 + waterTimer * vec2(0.04, -0.03);
        float crests = fract(sin(dot(floor(foamUV * 24.0), vec2(127.1, 311.7))) * 43758.5453);
        float foamStrength = clamp(foam * (0.5 + crests), 0.0, 1.0);
        // Brightness boost; tinted slightly by the sun colour for warmth.
        vec3 foamColor = mix(vec3(1.0), sunSpec.rgb, 0.3);
        gl_FragData[0].rgb = mix(gl_FragData[0].rgb, foamColor, foamStrength);
    }
#endif

#if @waterRefraction && @wobblyShores
    // wobbly water: hard-fade into refraction texture at extremely low depth, with a wobble based on normal mapping
    vec3 normalShoreRippleRain = texture2D(normalMap,normalCoords(UV, 2.0, 2.7, -1.0*waterTimer,  0.05,  0.1,  normal3)).rgb - 0.5
                               + texture2D(normalMap,normalCoords(UV, 2.0, 2.7,      waterTimer,  0.04, -0.13, normal4)).rgb - 0.5;
    float viewFactor = mix(abs(viewDir.z), 1.0, 0.2);
    float verticalWaterDepth = realWaterDepth * viewFactor; // an estimate
    float shoreOffset = verticalWaterDepth - (normal2.r + mix(0.0, normalShoreRippleRain.r, rainIntensity) + 0.15)*8.0;
    float fuzzFactor = min(1.0, 1000.0 / surfaceDepth) * viewFactor;
    shoreOffset *= fuzzFactor;
    shoreOffset = clamp(mix(shoreOffset, 1.0, clamp(linearDepth / WOBBLY_SHORE_FADE_DISTANCE, 0.0, 1.0)), 0.0, 1.0);
    gl_FragData[0].rgb = mix(rawRefraction, gl_FragData[0].rgb, shoreOffset);
#endif

#if @radialFog
    float radialDepth = distance(position.xyz, cameraPos);
#else
    float radialDepth = 0.0;
#endif

    gl_FragData[0] = applyFogAtDist(gl_FragData[0], radialDepth, linearDepth, far);

#if !@disableNormals
    gl_FragData[1].rgb = normalize(gl_NormalMatrix * normal) * 0.5 + 0.5;
#endif

    applyShadowDebugOverlay();
}
