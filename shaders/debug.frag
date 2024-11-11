#version 460 core

#include "color_passthrough.glsl"

uniform vec4 in_color;
uniform int color_passthrough;

layout (location=0) out vec4 FragColor;

void main()
{
    vec4 tex_color;

    if (color_passthrough == COLOR_PT_ALL) {
        tex_color = in_color;
    } else {
        tex_color = vec4(1.0, 0.0, 0.0, 1.0);
        if (color_passthrough >= COLOR_PT_ALPHA)
            tex_color.w = in_color.w;
    }

    FragColor = tex_color;
}
