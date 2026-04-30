#version 400 compatibility

layout(vertices = 3) out;

in vec3 tcModelPos[];
in vec3 tcNormal[];
in vec4 tcColor[];
in vec2 tcUv[];

out vec3 teModelPos[];
out vec3 teNormal[];
out vec4 teColor[];
out vec2 teUv[];

uniform float terrainTessMaxLevel;
uniform float terrainTessViewDistance;

float edgeLevel(vec3 a, vec3 b)
{
    // Use min(distance(eye, vertex)) for both endpoints — invariant when two
    // patches share an edge, so neighbouring patches agree on the level and
    // no T-junction crack appears between them.
    vec4 viewA = gl_ModelViewMatrix * vec4(a, 1.0);
    vec4 viewB = gl_ModelViewMatrix * vec4(b, 1.0);
    float dA = length(viewA.xyz);
    float dB = length(viewB.xyz);
    float d = min(dA, dB);

    float fade = clamp(1.0 - d / max(terrainTessViewDistance, 1.0), 0.0, 1.0);
    float lvl = mix(1.0, terrainTessMaxLevel, fade);

    // Quantise to integer to ensure exact agreement at shared edges.
    return max(1.0, floor(lvl + 0.5));
}

void main()
{
    if (gl_InvocationID == 0)
    {
        float l0 = edgeLevel(tcModelPos[1], tcModelPos[2]);
        float l1 = edgeLevel(tcModelPos[2], tcModelPos[0]);
        float l2 = edgeLevel(tcModelPos[0], tcModelPos[1]);

        gl_TessLevelOuter[0] = l0;
        gl_TessLevelOuter[1] = l1;
        gl_TessLevelOuter[2] = l2;
        gl_TessLevelInner[0] = max(max(l0, l1), l2);
    }

    teModelPos[gl_InvocationID] = tcModelPos[gl_InvocationID];
    teNormal[gl_InvocationID]   = tcNormal[gl_InvocationID];
    teColor[gl_InvocationID]    = tcColor[gl_InvocationID];
    teUv[gl_InvocationID]       = tcUv[gl_InvocationID];
}
