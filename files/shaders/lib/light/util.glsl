#ifndef LIB_LIGHT_UTIL
#define LIB_LIGHT_UTIL

#include "lib/light/struct.glsl"

uniform float clusterFar;

float fade(float x)
{
    x = clamp(x, 0.0, 1.0);
    x = 1.0 - x * x;
    x = 1.0 - x * x;
    return x;
}

float lambert(vec3 viewNormal, vec3 lightDir, vec3 viewDir)
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

float specularIntensity(vec3 viewNormal, vec3 viewDir, float shininess, vec3 lightDir)
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
float ggxSpecularIntensity(vec3 viewNormal, vec3 viewDir, float shininess, vec3 lightDir)
{
    float NdotL = dot(viewNormal, lightDir);
    if (NdotL <= 0.0)
        return 0.0;

    vec3 halfVec = normalize(lightDir - viewDir);
    float NdotH = max(dot(viewNormal, halfVec), 0.0);
    float NdotV = max(dot(viewNormal, -viewDir), 1e-4);
    float VdotH = max(dot(-viewDir, halfVec), 0.0);
    float roughness = clamp(1.0 - log2(max(shininess, 1.0)) / 8.0, 0.25, 1.0);
    float alpha = max(roughness * roughness, 0.0025);
    float alpha2 = alpha * alpha;
    float denom = (NdotH * NdotH) * (alpha2 - 1.0) + 1.0;
    float distribution = alpha2 / (3.14159265 * denom * denom);
    float k = (roughness + 1.0);
    k = (k * k) * 0.125;
    float geometryView = NdotV / (NdotV * (1.0 - k) + k);
    float geometryLight = NdotL / (NdotL * (1.0 - k) + k);
    float fresnel = 0.04 + (1.0 - 0.04) * pow(1.0 - VdotH, 5.0);

    return clamp((distribution * geometryView * geometryLight * fresnel) / (4.0 * NdotV * NdotL + 1e-4), 0.0, 2.0);
}

#define SPECULAR_INTENSITY(n, v, s, l) ggxSpecularIntensity(n, v, s, l)
#else
#define SPECULAR_INTENSITY(n, v, s, l) specularIntensity(n, v, s, l)
#endif

float calcAttenuation(PointLight light, float dist) {
    float attenuation = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    #if !@classicFalloff || @lightingMethodClustered
        // Fade illumination out to 0 when reaching the lights radius
        attenuation *= 1.0 - fade((dist / light.radius - 0.75) / 0.25);
    #endif
    return attenuation;
}

int getClusterTileIndex(vec2 screenRes, vec3 gridSize, float near, vec2 screenCoord, float viewSpaceZ) {
    int zTile = int((log(abs(viewSpaceZ) / near) * int(gridSize.z)) / log(clusterFar / near));
    vec2 tileSize = screenRes / vec2(gridSize.xy);
    ivec3 tile = ivec3(screenCoord / tileSize, zTile);
    int tileIndex = tile.x + (tile.y * int(gridSize.x)) + (tile.z * int(gridSize.x) * int(gridSize.y));

    return tileIndex;
}

void calcDirectionalLighting(DirectionalLight light, vec3 viewDir, vec3 viewNormal, float shininess, inout vec3 diffuseLight, inout vec3 ambientLight, inout vec3 specularLight) {
    vec3 dir = normalize(light.position.xyz);
    ambientLight += light.ambient.xyz;
    diffuseLight += light.diffuse.xyz * lambert(viewNormal, dir, viewDir);
    specularLight += light.specular.xyz * SPECULAR_INTENSITY(viewNormal, viewDir, shininess, dir);
}

// Ensure lights with bounds crossing the far cluster plane fade out.
// Without this there will be a hard cutoff where objects outside the cluster far plane are not lit.
float clusterFade(vec3 viewPos, float radius) {
#if @lightingMethodClustered
    return 1.0 - fade(clamp((-viewPos.z - (clusterFar - radius)) / radius, 0.0, 1.0));
#else
    return 1.0;
#endif
}

void calcPointLighting(PointLight light, vec3 viewDir, vec3 viewPos, vec3 viewNormal, float shininess, inout vec3 diffuseLight, inout vec3 ambientLight, inout vec3 specularLight) {
    vec3 lightPos = light.position.xyz - viewPos;
    float lightDistance = length(lightPos);

    // cull point lighting by radius, light is guaranteed to not fall outside this bound with our cutoff
#if !@classicFalloff
    if (lightDistance > light.radius)
        return;
#endif

    vec3 lightDir = lightPos / lightDistance;

    float attenuation = calcAttenuation(light, lightDistance) * clusterFade(viewPos, light.radius);

    diffuseLight += light.diffuse.xyz * lambert(viewNormal, lightDir, viewDir) * attenuation;
    ambientLight += light.ambient.xyz * attenuation;
    specularLight += light.specular.xyz * SPECULAR_INTENSITY(viewNormal, viewDir, shininess, lightDir) * attenuation;
}

#endif
