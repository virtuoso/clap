#version 460 core

#include "shader_constants.h"
#include "downsample.glsl"
#include "bloom-level.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;

/*
 * One bloom pyramid level down: a 2x downsample of the level above,
 * with the carried target level moving one closer to this one
 */
void main()
{
    vec4 color = downsample_13tap(model_tex, pass_tex);

    FragColor = vec4(color.rgb, bloom_level_step(color));
}
