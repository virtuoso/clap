#version 460 core

#include "shader_constants.h"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;

#include "ubo_postproc.glsl"

void main()
{
    vec4 tex_color = texture(model_tex, pass_tex);

    tex_color.rgb = (tex_color.rgb - 0.5) * (1.0 + contrast) + 0.5;
    FragColor = tex_color;
}