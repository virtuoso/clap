#version 460 core

#include "color_passthrough.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

uniform sampler2D model_tex;
uniform vec4 in_color;
uniform int color_passthrough;

const float contrast = 0.3;

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

    tex_color.rgb = (tex_color.rgb - 0.5) * (1.0 + contrast) + 0.5;
    FragColor = tex_color;
}