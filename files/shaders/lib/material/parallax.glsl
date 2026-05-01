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

#endif
