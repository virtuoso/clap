#version 460 core

#include "shader_constants.h"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;

#include "ubo_color_pt.glsl"

void main()
{
    vec4 tex_color;

    if (color_passthrough == COLOR_PT_ALL) {
    	tex_color = in_color;
    } else {
        tex_color = texture(model_tex, pass_tex);
        if (color_passthrough == COLOR_PT_ALPHA)
            tex_color.w = in_color.w;
    }

    FragColor = tex_color;
}
