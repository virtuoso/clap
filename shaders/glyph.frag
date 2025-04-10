#version 460 core

#include "shader_constants.h"

uniform sampler2D model_tex;
layout (std140, binding = UBO_BINDING_color_pt) uniform color_pt {
    vec4 in_color;
    int color_passthrough;
};

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
