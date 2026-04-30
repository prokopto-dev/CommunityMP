#version 400 compatibility

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4 : require
#endif

// Pass-through vertex stage for tessellated terrain.
// Position/transform/lighting work happens in the TES after displacement,
// so the VS only forwards model-space attributes to the TCS.

out vec3 tcModelPos;
out vec3 tcNormal;
out vec4 tcColor;
out vec2 tcUv;

void main()
{
    tcModelPos = gl_Vertex.xyz;
    tcNormal = gl_Normal.xyz;
    tcColor = gl_Color;
    tcUv = gl_MultiTexCoord0.xy;
}
