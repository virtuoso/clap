#version 460 core

#include "color_pt.glsl"

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

void main()
{
    vec4 tex_color = texture(model_tex, pass_tex);

    FragColor = color_override(tex_color);
}
