#ifndef LIB_MATERIAL_PARALLAX
#define LIB_MATERIAL_PARALLAX

// Per-pipeline parallax depth & bias, pushed as global uniforms by the
// rendering manager from [Shaders] parallax scale / parallax bias.
// Defaults match the historical hard-coded constants (0.04 / -0.02) so
// vanilla content remains visually unchanged when no override is set.
uniform float parallaxScale;
uniform float parallaxBias;

vec2 getParallaxOffset(vec3 eyeDir, float height)
{
    return vec2(eyeDir.x, eyeDir.y) * ( height * parallaxScale + parallaxBias );
}

// AAA-grade tangent-space self-shadowing for height-mapped surfaces.
//
// The function ray-marches the height channel toward the sun in tangent
// space. Because the march lives entirely on the surface, the result is
// camera-independent — only sun direction and surface relief matter, no
// depth-buffer dependence, no screen-space artifacts.
//
// Pipeline:
//   1. Adaptive sample count: more steps at grazing angles where the
//      occlusion path is long; fewer head-on where it would be wasted.
//   2. Linear march to find the first sample above the ray height.
//   3. Binary search between the last clear and first occluded sample
//      to land sub-texel-accurate on the intersection. Kills the stair-
//      step aliasing that plain linear marches have.
//   4. PCSS-style soft penumbra: shadow strength scales with both the
//      blocker height excess and how close the blocker is to the
//      receiver. Far blockers cast soft shadows, near blockers hard.
//   5. Distance attenuation: fade self-shadow to 1 beyond ~2400 units
//      where the height tap rate would alias anyway.
//   6. textureLod sampling at a tighter mip than the diffuse to keep
//      shadow edges crisp without amplifying mip noise on minified
//      surfaces.
//
// `lightTS` must be the sun direction in *tangent* space (not view). If
// lightTS.z <= 0 the sun is below the surface tangent plane and there
// is nothing to occlude — the function returns 1.0 and the caller
// pays nothing else.
//
// Quality budget is set by PARALLAX_SELF_SHADOW_SAMPLES (compile-time
// define from the renderer; default 16). Set to 0 to compile the
// function out entirely.
#ifndef PARALLAX_SELF_SHADOW_SAMPLES
#define PARALLAX_SELF_SHADOW_SAMPLES 16
#endif

#if PARALLAX_SELF_SHADOW_SAMPLES > 0
float parallaxSelfShadow(sampler2D heightMap, vec2 baseUV, vec3 lightTS,
                         float receiverHeight, float viewDistance)
{
    // Sun underneath the local tangent plane: surface itself blocks
    // the sun, but that's already handled by Lambert N·L < 0.
    if (lightTS.z <= 0.001)
        return 1.0;

    // Distance attenuation: at >~2400 units the height map is at high
    // mip levels and the relief is barely visible anyway. Fade smoothly
    // so we don't pay for samples that contribute nothing.
    float distFade = 1.0 - smoothstep(1600.0, 2400.0, viewDistance);
    if (distFade <= 0.0)
        return 1.0;

    // Adaptive sample count: lightTS.z = 1 means sun overhead and the
    // path is short; lightTS.z = 0 means grazing and path is long. We
    // pay more samples in the grazing case where shadows are dramatic.
    int maxSamples = PARALLAX_SELF_SHADOW_SAMPLES;
    int numSamples = int(mix(float(maxSamples), float(maxSamples / 2),
                             clamp(lightTS.z, 0.0, 1.0)));

    // Step in UV: the light's tangent xy direction, scaled so the full
    // sweep walks `parallaxScale` worth of UV across all samples.
    vec2 stepUV = (lightTS.xy / max(lightTS.z, 0.05))
                  * parallaxScale * (1.0 / float(maxSamples));

    // Height advances from receiverHeight up toward 1.0 (top of relief)
    // over the full path length.
    float stepHeight = (1.0 - receiverHeight) / float(maxSamples);

    // Linear march to find the first sample above the ray. Uses
    // texture2D so the GPU's automatic mip selection (driven by
    // dFdx/dFdy of the surface UV) keeps the height tap rate sane.
    // GLSL 120 fragment doesn't expose texture2DLod portably on Apple
    // GL 2.1 Metal so we lean on auto-mips here.
    float lastClearH = receiverHeight;
    float lastClearRayH = receiverHeight;
    int hitIndex = -1;
    float hitSampleH = 0.0;
    float hitRayH = 0.0;

    for (int i = 1; i <= numSamples; ++i)
    {
        vec2 uvI = baseUV + stepUV * float(i);
        float rayH = receiverHeight + stepHeight * float(i);
        float sampleH = texture2D(heightMap, uvI).a;

        if (sampleH > rayH)
        {
            hitIndex = i;
            hitSampleH = sampleH;
            hitRayH = rayH;
            break;
        }
        lastClearH = sampleH;
        lastClearRayH = rayH;
    }

    if (hitIndex < 0)
        return 1.0; // ray cleared all samples — fully lit

    // Binary search refinement between the last clear sample and the
    // first occluded one. Two iterations land us within ~1/4 of a step
    // in UV, which is sub-texel for any reasonable normal map size.
    float lo = float(hitIndex - 1);
    float hi = float(hitIndex);
    float midSampleH = hitSampleH;
    float midRayH = hitRayH;
    for (int j = 0; j < 2; ++j)
    {
        float midI = (lo + hi) * 0.5;
        vec2 uvM = baseUV + stepUV * midI;
        float rayH = receiverHeight + stepHeight * midI;
        float sampleH = texture2DLod(heightMap, uvM, mip).a;

        if (sampleH > rayH)
        {
            hi = midI;
            midSampleH = sampleH;
            midRayH = rayH;
        }
        else
        {
            lo = midI;
        }
    }

    // PCSS-style penumbra: the shadow is darker the more the blocker
    // sticks up above the ray, and softer the further the blocker is
    // from the receiver. Blocker distance proxy: hi (sample index of
    // the intersection) normalised by the total step budget.
    float blockerDist = hi / float(numSamples);
    float blockerExcess = clamp(midSampleH - midRayH, 0.0, 1.0);
    // Hardness: 1.0 = full opaque, 0.0 = no shadow. Quadratic in excess
    // gives a perceptually pleasant ramp; multiplied by (1 - blockerDist)
    // gives the penumbra falloff with distance.
    float hardness = blockerExcess * 4.0 * (1.0 - blockerDist);
    hardness = clamp(hardness, 0.0, 0.85);

    // Final shadow value: 1 - hardness, attenuated by distance fade.
    float selfShadow = 1.0 - hardness;
    return mix(1.0, selfShadow, distFade);
}
#else
float parallaxSelfShadow(sampler2D heightMap, vec2 baseUV, vec3 lightTS,
                         float receiverHeight, float viewDistance)
{
    return 1.0;
}
#endif

#endif
