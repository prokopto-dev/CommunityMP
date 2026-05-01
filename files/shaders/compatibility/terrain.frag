#version 120

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

varying vec2 uv;

uniform sampler2D diffuseMap;

#if @normalMap
uniform sampler2D normalMap;
#endif

#if @blendMap
uniform sampler2D blendMap;
#endif

#if @terrainProceduralBump
uniform sampler2D terrainProceduralBumpMap;
uniform float terrainProceduralBumpStrength;
uniform float terrainProceduralBumpScale;
varying vec3 passWorldPos;
#endif

varying float euclideanDepth;
varying float linearDepth;

#define PER_PIXEL_LIGHTING (@normalMap || @specularMap || @forcePPL)

#if !PER_PIXEL_LIGHTING
centroid varying vec3 passLighting;
centroid varying vec3 passSpecular;
centroid varying vec3 shadowDiffuseLighting;
centroid varying vec3 shadowSpecularLighting;
#endif
varying vec3 passViewPos;
varying vec3 passNormal;

uniform vec2 screenRes;
uniform float far;

#include "vertexcolors.glsl"
#include "shadows_fragment.glsl"
#include "lib/light/lighting.glsl"
#include "lib/material/parallax.glsl"
#include "fog.glsl"
#include "compatibility/normals.glsl"

void main()
{
    vec2 adjustedUV = (gl_TextureMatrix[0] * vec4(uv, 0.0, 1.0)).xy;

#if @parallax
    float height = texture2D(normalMap, adjustedUV).a;
    adjustedUV += getParallaxOffset(transpose(normalToViewMatrix) * normalize(-passViewPos), height);
#endif
    vec4 diffuseTex = texture2D(diffuseMap, adjustedUV);
    gl_FragData[0] = vec4(diffuseTex.xyz, 1.0);

    vec4 diffuseColor = getDiffuseColor();
    gl_FragData[0].a *= diffuseColor.a;

#if @blendMap
    vec2 blendMapUV = (gl_TextureMatrix[1] * vec4(uv, 0.0, 1.0)).xy;
    gl_FragData[0].a *= texture2D(blendMap, blendMapUV).a;
#endif

#if @normalMap
    vec4 normalTex = texture2D(normalMap, adjustedUV);
    vec3 normal = normalTex.xyz * 2.0 - 1.0;
#if @reconstructNormalZ
    normal.z = sqrt(1.0 - dot(normal.xy, normal.xy));
#endif
    // Match objects.frag: stretch tangent XY so PBR-pipeline normal
    // maps (which come out nearly flat) produce visible bump shading.
    normal.xy *= 4.0;
    vec3 viewNormal = normalToView(normal);
#else
    vec3 viewNormal = normalize(gl_NormalMatrix * passNormal);
#endif

#if @terrainProceduralBump
    // Perturb the view-space normal using the procedural bump heightmap.
    // Sample the height + two epsilon-offset taps to compute the gradient
    // in tangent space, project onto world tangent plane (Z-up), then
    // transform to view space.
    {
        vec2 bumpUV = passWorldPos.xy * terrainProceduralBumpScale;
        const float eps = 0.5;
        float h0 = texture2D(terrainProceduralBumpMap, bumpUV).r;
        float hx = texture2D(terrainProceduralBumpMap, bumpUV + vec2(eps * terrainProceduralBumpScale, 0.0)).r;
        float hy = texture2D(terrainProceduralBumpMap, bumpUV + vec2(0.0, eps * terrainProceduralBumpScale)).r;
        // Slope mask so we don't perturb cliffs (where it would look broken).
        float slopeMask = clamp(passNormal.z, 0.0, 1.0);
        slopeMask *= slopeMask;
        vec3 bumpWorld = vec3(-(hx - h0), -(hy - h0), 0.05) * terrainProceduralBumpStrength * slopeMask;
        vec3 bumpView = gl_NormalMatrix * bumpWorld;
        viewNormal = normalize(viewNormal + bumpView);
    }
#endif

    // Bump self-shadow on the terrain height channel (same algorithm
    // as objects.frag). Camera-independent because tangent-space.
    float bumpSelfShadow = 1.0;
#if @parallax && @normalMap && PARALLAX_SELF_SHADOW_SAMPLES > 0
    {
        float receiverHeight = texture2D(normalMap, adjustedUV).a;
        vec3 sunVS = normalize(lcalcPosition(0));
        vec3 sunTS = transpose(normalToViewMatrix) * sunVS;
        bumpSelfShadow = parallaxSelfShadow(
            normalMap, adjustedUV, sunTS, receiverHeight, linearDepth);
    }
#endif

    float shadowing = unshadowedLightRatio(linearDepth);
    vec3 lighting, specular;
#if !PER_PIXEL_LIGHTING
    lighting = passLighting + shadowDiffuseLighting * shadowing;
    specular = passSpecular + shadowSpecularLighting * shadowing;
#else
#if @specularMap
    float shininess = 128.0; // TODO: make configurable
    vec3 specularColor = vec3(diffuseTex.a);
#else
    float shininess = gl_FrontMaterial.shininess;
    vec3 specularColor = getSpecularColor().xyz;
#endif
    vec3 diffuseLight, ambientLight, specularLight;
    doLighting(passViewPos, viewNormal, shininess, shadowing, diffuseLight, ambientLight, specularLight);
    diffuseLight *= bumpSelfShadow;
    specularLight *= bumpSelfShadow;
    lighting = diffuseColor.xyz * diffuseLight + getAmbientColor().xyz * ambientLight + getEmissionColor().xyz;
    specular = specularColor * specularLight;
#endif

    clampLightingResult(lighting);
    gl_FragData[0].xyz = gl_FragData[0].xyz * lighting + specular;

    gl_FragData[0] = applyFogAtDist(gl_FragData[0], euclideanDepth, linearDepth, far);

#if !@disableNormals && @writeNormals
    gl_FragData[1].xyz = viewNormal * 0.5 + 0.5;
#endif

    applyShadowDebugOverlay();
}
