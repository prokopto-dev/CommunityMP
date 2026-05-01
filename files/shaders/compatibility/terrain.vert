#version 120

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

#include "lib/core/vertex.h.glsl"
varying vec2 uv;
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

#if @terrainProceduralBump
varying vec3 passWorldPos;
#endif

#include "vertexcolors.glsl"
#include "shadows_vertex.glsl"
#include "compatibility/normals.glsl"

#include "lib/light/lighting.glsl"
#include "lib/view/depth.glsl"

#if @terrainDisplacement
uniform float terrainTessDisplacementScale;

float terrainHash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float terrainVnoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = terrainHash21(i);
    float b = terrainHash21(i + vec2(1.0, 0.0));
    float c = terrainHash21(i + vec2(0.0, 1.0));
    float d = terrainHash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 2.0 - 1.0;
}

float terrainFbm(vec2 p)
{
    float v = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < 3; ++i)
    {
        v += amp * terrainVnoise(p * freq);
        freq *= 2.07;
        amp *= 0.5;
    }
    return v;
}
#endif

#if @grassWind
uniform float osg_SimulationTime;
uniform float grassWindAmplitude;
uniform float grassWindSpeed;
uniform float grassWindFrequency;
uniform vec2 grassWindDir;
#endif

void main(void)
{
    vec4 modelVertex = gl_Vertex;
    vec3 displacedNormal = gl_Normal.xyz;

#if @grassWind
    // Grass mask from vertex colour. Morrowind paints grass-textured terrain
    // patches with a green-dominant colour; reading gl_Color.g vs the other
    // channels gives a free per-vertex grass weight.
    float grassMask = clamp(gl_Color.g - 0.55 * (gl_Color.r + gl_Color.b) * 0.5, 0.0, 1.0);
    grassMask = grassMask * grassMask;
    // Travelling wave along grassWindDir; phase advances with simulation
    // time so the lawn ripples continuously even when the player stands
    // still. Two octaves for richer motion.
    float phase = dot(modelVertex.xy, grassWindDir) * grassWindFrequency
                + osg_SimulationTime * grassWindSpeed;
    float wave = sin(phase) + 0.4 * sin(phase * 2.13 + 1.7);
    modelVertex.z += wave * grassWindAmplitude * grassMask;
#endif
#if @terrainDisplacement
    // Software fallback for hardware tessellation. Adds procedural relief at
    // existing terrain vertices. Combined with CPU-side mesh densification
    // (terrain.tessellation_emulation_factor) the effect becomes pronounced.
    float slopeMask = clamp(gl_Normal.z, 0.0, 1.0);
    slopeMask = slopeMask * slopeMask;

    const float fbmFreq = 0.015;
    const float eps = 8.0; // world units; coarse central-difference step.
    float h  = terrainFbm( modelVertex.xy             * fbmFreq) * slopeMask;
    float hx = terrainFbm((modelVertex.xy + vec2(eps, 0.0)) * fbmFreq) * slopeMask;
    float hy = terrainFbm((modelVertex.xy + vec2(0.0, eps)) * fbmFreq) * slopeMask;

    modelVertex.xyz += gl_Normal.xyz * (h * terrainTessDisplacementScale);

    // Recompute the surface normal from the analytic gradient of the height
    // field, then blend with the original to keep large-scale terrain shape
    // intact while letting the displacement drive lighting at small scale.
    vec3 dpdx = vec3(eps, 0.0, (hx - h) * terrainTessDisplacementScale);
    vec3 dpdy = vec3(0.0, eps, (hy - h) * terrainTessDisplacementScale);
    vec3 fbmNormal = normalize(cross(dpdx, dpdy));
    displacedNormal = normalize(mix(gl_Normal.xyz, fbmNormal, 0.5));
#endif

    gl_Position = modelToClip(modelVertex);

    vec4 viewPos = modelToView(modelVertex);
    gl_ClipVertex = viewPos;
    euclideanDepth = length(viewPos.xyz);
    linearDepth = getLinearDepth(gl_Position.z, viewPos.z);

    passColor = gl_Color;
    passNormal = displacedNormal;
    passViewPos = viewPos.xyz;
    normalToViewMatrix = gl_NormalMatrix;

#if @terrainProceduralBump
    // Pass world-space xy to the fragment so it can index the procedural
    // bump heightmap independently of the chunk-local UV.
    passWorldPos = modelVertex.xyz;
#endif

#if @normalMap
    mat3 tbnMatrix = generateTangentSpace(vec4(1.0, 0.0, 0.0, 1.0), passNormal);
    tbnMatrix[0] = -normalize(cross(tbnMatrix[2], tbnMatrix[1])); // our original tangent was not at a 90 degree angle to the normal, so we need to rederive it
    normalToViewMatrix *= tbnMatrix;
#endif

#if !PER_PIXEL_LIGHTING || @shadows_enabled
    vec3 viewNormal = normalize(gl_NormalMatrix * passNormal);
#endif

#if !PER_PIXEL_LIGHTING
    vec3 diffuseLight, ambientLight, specularLight;
    doLighting(viewPos.xyz, viewNormal, gl_FrontMaterial.shininess, diffuseLight, ambientLight, specularLight, shadowDiffuseLighting, shadowSpecularLighting);
    passLighting = getDiffuseColor().xyz * diffuseLight + getAmbientColor().xyz * ambientLight + getEmissionColor().xyz;
    passSpecular = getSpecularColor().xyz * specularLight;
    clampLightingResult(passLighting);
    shadowDiffuseLighting *= getDiffuseColor().xyz;
    shadowSpecularLighting *= getSpecularColor().xyz;
#endif

    uv = gl_MultiTexCoord0.xy;

#if (@shadows_enabled)
    setupShadowCoords(viewPos, viewNormal);
#endif
}
