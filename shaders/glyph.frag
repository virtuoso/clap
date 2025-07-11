#version 460 core

#include "shader_constants.h"
#include "ubo_color_pt.glsl"

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

void main()
{
    vec4 tex_color = texture(model_tex, pass_tex);

    if (color_passthrough == COLOR_PT_ALL && length(tex_color) > 0.1) {
    	tex_color = in_color;
    } else if (color_passthrough == COLOR_PT_ALPHA && length(tex_color) > 0.1) {
        tex_color.w = tex_color.w * in_color.w;
    }

    FragColor = tex_color;
}
