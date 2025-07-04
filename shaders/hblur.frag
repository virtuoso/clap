#version 460 core

#include "shader_constants.h"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;

void main()
{
    float pixsz = 1.0 / textureSize(model_tex, 0).x;
    vec4 color = vec4(0.0);
    color += texture(model_tex, pass_tex + vec2(-5.0 * pixsz, 0.0)) * 0.0093;
    color += texture(model_tex, pass_tex + vec2(-4.0 * pixsz, 0.0)) * 0.028002;
    color += texture(model_tex, pass_tex + vec2(-3.0 * pixsz, 0.0)) * 0.065984;
    color += texture(model_tex, pass_tex + vec2(-2.0 * pixsz, 0.0)) * 0.121703;
    color += texture(model_tex, pass_tex + vec2(-1.0 * pixsz, 0.0)) * 0.175713;
    color += texture(model_tex, pass_tex + vec2( 0.0 * pixsz, 0.0)) * 0.198596;
    color += texture(model_tex, pass_tex + vec2( 1.0 * pixsz, 0.0)) * 0.175713;
    color += texture(model_tex, pass_tex + vec2( 2.0 * pixsz, 0.0)) * 0.121703;
    color += texture(model_tex, pass_tex + vec2( 3.0 * pixsz, 0.0)) * 0.065984;
    color += texture(model_tex, pass_tex + vec2( 4.0 * pixsz, 0.0)) * 0.028002;
    color += texture(model_tex, pass_tex + vec2( 5.0 * pixsz, 0.0)) * 0.0093;

    FragColor = color;
}
