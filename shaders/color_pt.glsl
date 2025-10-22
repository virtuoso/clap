#ifndef SHADERS_COLOR_PT_GLSL
#define SHADERS_COLOR_PT_GLSL

#include "shader_constants.h"
#include "ubo_color_pt.glsl"

bool nonempty(vec4 color)
{
    return dot(color, color) > 1e-3;
}

vec4 color_override(vec4 color)
{
    vec4 out_color = color;

    if ((color_passthrough & COLOR_PT_SET_RGB) != 0)
        out_color.rgb = in_color.rgb;
    else if ((color_passthrough & COLOR_PT_REPLACE_RGB) != 0 && nonempty(color))
        out_color.rgb = in_color.rgb;

    if ((color_passthrough & COLOR_PT_SET_ALPHA) != 0)
        out_color.a = in_color.a;
    else if ((color_passthrough & COLOR_PT_BLEND_ALPHA) != 0)
        out_color.a *= in_color.a;
    else if ((color_passthrough & COLOR_PT_REPLACE_ALPHA) != 0 && color.a > 0.0)
        out_color.a = in_color.a;

    return out_color;
}

#endif /* SHADERS_COLOR_PT_GLSL */
