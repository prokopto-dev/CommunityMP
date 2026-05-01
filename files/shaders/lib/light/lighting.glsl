#ifndef LIB_LIGHT_LIGHTING
#define LIB_LIGHT_LIGHTING

#include "lighting_util.glsl"

float calcLambert(vec3 viewNormal, vec3 lightDir, vec3 viewDir)
{
    float lambert = dot(viewNormal, lightDir);
#ifndef GROUNDCOVER
    lambert = max(lambert, 0.0);
#else
    float eyeCosine = dot(viewNormal, viewDir);
    if (lambert < 0.0)
    {
        lambert = -lambert;
        eyeCosine = -eyeCosine;
    }
    lambert *= clamp(-8.0 * (1.0 - 0.3) * eyeCosine + 1.0, 0.3, 1.0);
#endif
    return lambert;
}

float calcSpecIntensity(vec3 viewNormal, vec3 viewDir, float shininess, vec3 lightDir)
{
    if (dot(viewNormal, lightDir) > 0.0)
    {
        vec3 halfVec = normalize(lightDir - viewDir);
        float NdotH = max(dot(viewNormal, halfVec), 0.0);
        return pow(NdotH, shininess);
    }

    return 0.0;
}

#if @pbrSpecular
// GGX (Trowbridge-Reitz) normal distribution. Energy-conserving microfacet
// model. roughness in [0..1]; 0 = mirror, 1 = matte. Returns the spec
// intensity weight to multiply the light's specular colour by.
float calcGGXSpec(vec3 viewNormal, vec3 viewDir, vec3 lightDir, float roughness, float F0)
{
    float NdotL = dot(viewNormal, lightDir);
    if (NdotL <= 0.0) return 0.0;

    vec3 halfVec = normalize(lightDir - viewDir);
    float NdotH = max(dot(viewNormal, halfVec), 0.0);
    float NdotV = max(dot(viewNormal, -viewDir), 1e-4);
    float VdotH = max(dot(-viewDir, halfVec), 0.0);

    // Avoid mirror-singularity on perfectly smooth surfaces.
    float a = max(roughness * roughness, 0.0025);
    float a2 = a * a;

    // GGX NDF.
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    float D = a2 / (3.14159265 * denom * denom);

    // Smith G1 separable shadowing/masking, both directions.
    float k = (roughness + 1.0); k = (k * k) * 0.125;
    float G_v = NdotV / (NdotV * (1.0 - k) + k);
    float G_l = NdotL / (NdotL * (1.0 - k) + k);
    float G = G_v * G_l;

    // Schlick Fresnel; F0 is the dielectric/metallic base reflectance.
    float F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    // Clamp ceiling kept conservative so a low-roughness peak can't drown
    // the diffuse term — that was making bump detail disappear in lit
    // regions while remaining visible in shadow (where spec is 0).
    return clamp((D * G * F) / (4.0 * NdotV * NdotL + 1e-4), 0.0, 2.0);
}
#endif

#if @pbrSpecular
// PBR-aware spec intensity. Roughness is derived from `shininess` if a
// dedicated value isn't available (legacy meshes), else passed through.
// F0 default 0.04 = dielectric. Higher = metallic / lacquered.
float pbrSpec(vec3 viewNormal, vec3 viewDir, vec3 lightDir, float shininess)
{
    // Phong shininess range [1..256] -> roughness curve. Floor at 0.25:
    // PBR spec maps from the pix2pix pipeline often have alpha=1.0, which
    // pushed shininess to 255 and roughness to ~0.04 (near-mirror) — that
    // produced a single bright highlight large enough to mask diffuse
    // bump variation in lit areas. 0.25 keeps materials matte enough that
    // the normal-mapped diffuse stays legible.
    float roughness = clamp(1.0 - log2(max(shininess, 1.0)) / 8.0, 0.25, 1.0);
    return calcGGXSpec(viewNormal, viewDir, lightDir, roughness, 0.04);
}
#define _SPEC_FN(n,v,l,s) pbrSpec(n,v,l,s)
#else
#define _SPEC_FN(n,v,l,s) calcSpecIntensity(n,v,s,l)
#endif

#if PER_PIXEL_LIGHTING
void doLighting(vec3 viewPos, vec3 viewNormal, float shininess, float shadowing, out vec3 diffuseLight, out vec3 ambientLight, out vec3 specularLight)
#else
void doLighting(vec3 viewPos, vec3 viewNormal, float shininess, out vec3 diffuseLight, out vec3 ambientLight, out vec3 specularLight, out vec3 shadowDiffuse, out vec3 shadowSpecular)
#endif
{
    vec3 viewDir = normalize(viewPos);
    shininess = max(shininess, 1e-4);

    vec3 sunDir = normalize(lcalcPosition(0));
    diffuseLight = lcalcDiffuse(0) * calcLambert(viewNormal, sunDir, viewDir);
    ambientLight = gl_LightModel.ambient.xyz;
    specularLight = lcalcSpecular(0).xyz * _SPEC_FN(viewNormal, viewDir, sunDir, shininess);
#if PER_PIXEL_LIGHTING
    diffuseLight *= shadowing;
    specularLight *= shadowing;
#else
    shadowDiffuse = diffuseLight;
    shadowSpecular = specularLight;
    diffuseLight = vec3(0.0);
    specularLight = vec3(0.0);
#endif

    for (int i = @startLight; i < PointLightCount; ++i)
    {
#if @lightingMethodUBO
        int lightIndex = PointLightIndex[i];
#else
        int lightIndex = i;
#endif
        vec3 lightPos = lcalcPosition(lightIndex) - viewPos;
        float lightDistance = length(lightPos);

        // cull point lighting by radius, light is guaranteed to not fall outside this bound with our cutoff
#if !@classicFalloff
        if (lightDistance > lcalcRadius(lightIndex) * 2.0)
            continue;
#endif

        vec3 lightDir = lightPos / lightDistance;

        float illumination = lcalcIllumination(lightIndex, lightDistance);
        diffuseLight += lcalcDiffuse(lightIndex) * calcLambert(viewNormal, lightDir, viewDir) * illumination;
        ambientLight += lcalcAmbient(lightIndex) * illumination;
        specularLight += lcalcSpecular(lightIndex).xyz * _SPEC_FN(viewNormal, viewDir, lightDir, shininess) * illumination;
    }
}

#endif
