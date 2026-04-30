#version 400 compatibility

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4 : require
#endif

layout(triangles, fractional_odd_spacing, ccw) in;

#include "lib/core/vertex.h.glsl"

in vec3 teModelPos[];
in vec3 teNormal[];
in vec4 teColor[];
in vec2 teUv[];

// Outputs to fragment shader. Names must match the varyings in compatibility
// includes (vertexcolors.glsl, shadows_vertex.glsl, normals.glsl) so that
// the fragment side resolves them as 'in' through the compatibility profile.
out vec2 uv;
out float euclideanDepth;
out float linearDepth;
out vec3 passViewPos;
out vec3 passNormal;

#define PER_PIXEL_LIGHTING (@normalMap || @specularMap || @forcePPL)
#if !PER_PIXEL_LIGHTING
centroid out vec3 passLighting;
centroid out vec3 passSpecular;
centroid out vec3 shadowDiffuseLighting;
centroid out vec3 shadowSpecularLighting;
#endif

#include "compatibility/vertexcolors.glsl"
#include "compatibility/shadows_vertex.glsl"
#include "compatibility/normals.glsl"

#include "lib/light/lighting.glsl"
#include "lib/view/depth.glsl"

uniform float terrainTessDisplacementScale;

// Cheap deterministic hash and a 3-octave value-noise FBM. No texture fetch,
// works on any GPU with GL 4.0+. Frequency is in world-units; amplitude is in
// world-units after multiplication by terrainTessDisplacementScale.
float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float vnoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 2.0 - 1.0;
}

float fbm(vec2 p)
{
    float v = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < 3; ++i)
    {
        v += amp * vnoise(p * freq);
        freq *= 2.07;
        amp *= 0.5;
    }
    return v;
}

void main()
{
    vec3 b = gl_TessCoord.xyz;

    // Barycentric interpolation of the model-space attributes.
    vec3 modelPos = b.x * teModelPos[0] + b.y * teModelPos[1] + b.z * teModelPos[2];
    vec3 normal   = normalize(b.x * teNormal[0]   + b.y * teNormal[1]   + b.z * teNormal[2]);
    vec4 color    = b.x * teColor[0]    + b.y * teColor[1]    + b.z * teColor[2];
    vec2 uv0      = b.x * teUv[0]       + b.y * teUv[1]       + b.z * teUv[2];

    // Procedural displacement along the surface normal. Modulated by slope so
    // steep cliffs do not gain noise that would visibly explode them.
    float slopeMask = clamp(normal.z, 0.0, 1.0); // z is up in Morrowind world space
    slopeMask = slopeMask * slopeMask;
    float h = fbm(modelPos.xy * 0.015) * slopeMask;
    modelPos += normal * (h * terrainTessDisplacementScale);

    vec4 modelPos4 = vec4(modelPos, 1.0);
    gl_Position = modelToClip(modelPos4);

    vec4 viewPos = modelToView(modelPos4);
    gl_ClipVertex = viewPos;
    euclideanDepth = length(viewPos.xyz);
    linearDepth = getLinearDepth(gl_Position.z, viewPos.z);

    passColor = color;
    passNormal = normal;
    passViewPos = viewPos.xyz;
    normalToViewMatrix = gl_NormalMatrix;

#if @normalMap
    mat3 tbnMatrix = generateTangentSpace(vec4(1.0, 0.0, 0.0, 1.0), passNormal);
    tbnMatrix[0] = -normalize(cross(tbnMatrix[2], tbnMatrix[1]));
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

    uv = uv0;

#if (@shadows_enabled)
    setupShadowCoords(viewPos, viewNormal);
#endif
}
