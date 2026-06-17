#ifndef LIB_MATERIAL_PARALLAX
#define LIB_MATERIAL_PARALLAX

uniform float parallaxScale;
uniform float parallaxBias;

vec2 getParallaxOffset(vec3 eyeDir, float height)
{
    return vec2(eyeDir.x, eyeDir.y) * ( height * parallaxScale + parallaxBias );
}

#ifndef PARALLAX_SELF_SHADOW_SAMPLES
#define PARALLAX_SELF_SHADOW_SAMPLES 16
#endif

#if PARALLAX_SELF_SHADOW_SAMPLES > 0
float parallaxSelfShadow(sampler2D heightMap, vec2 baseUV, vec3 lightTS, float receiverHeight, float viewDistance)
{
    if (lightTS.z <= 0.001)
        return 1.0;

    float distFade = 1.0 - smoothstep(1600.0, 2400.0, viewDistance);
    if (distFade <= 0.0)
        return 1.0;

    int maxSamples = PARALLAX_SELF_SHADOW_SAMPLES;
    int numSamples = int(mix(float(maxSamples), float(maxSamples / 2), clamp(lightTS.z, 0.0, 1.0)));

    float lenXY = max(length(lightTS.xy), 0.001);
    float pathLen = parallaxScale / lenXY;
    vec2 stepUV = lightTS.xy * (pathLen / float(maxSamples));
    float stepHeight = lightTS.z * (pathLen / float(maxSamples));

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
    }

    if (hitIndex < 0)
        return 1.0;

    float lo = float(hitIndex - 1);
    float hi = float(hitIndex);
    float midSampleH = hitSampleH;
    float midRayH = hitRayH;
    for (int j = 0; j < 2; ++j)
    {
        float midI = (lo + hi) * 0.5;
        vec2 uvM = baseUV + stepUV * midI;
        float rayH = receiverHeight + stepHeight * midI;
        float sampleH = texture2D(heightMap, uvM).a;

        if (sampleH > rayH)
        {
            hi = midI;
            midSampleH = sampleH;
            midRayH = rayH;
        }
        else
            lo = midI;
    }

    float blockerDist = hi / float(numSamples);
    float blockerExcess = clamp(midSampleH - midRayH, 0.0, 1.0);
    float hardness = clamp(blockerExcess * 4.0 * (1.0 - blockerDist), 0.0, 0.85);
    return mix(1.0, 1.0 - hardness, distFade);
}
#else
float parallaxSelfShadow(sampler2D heightMap, vec2 baseUV, vec3 lightTS, float receiverHeight, float viewDistance)
{
    return 1.0;
}
#endif

#endif
