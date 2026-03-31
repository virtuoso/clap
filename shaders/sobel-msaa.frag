#version 460 core
#extension GL_EXT_samplerless_texture_functions : require

#include "shader_constants.h"

layout (location=0) in vec2 pass_tex;
layout (binding=SAMPLER_BINDING_model_tex) uniform texture2DMS model_tex;
layout (binding=SAMPLER_BINDING_normal_map) uniform texture2DMS normal_map;

layout (location=0) out vec4 FragColor;

#include "edge_filter.glsl"
#include "ubo_postproc.glsl"

/*
 * Workaround for a tint SPIR-V reader ICE in lower/texture.cc when an MS
 * texture is accessed through call chains of differing depths. Both
 * laplace_float() and sobel_filter_2d() reach texel_fetch_2dms() three
 * levels deep, so route the center fetch through the same depth.
 */
vec4 fetch_center_inner(texture2DMS tex, vec2 uv)
{
    return texel_fetch_2dms(tex, uv);
}

vec4 fetch_center(texture2DMS tex, vec2 uv)
{
    return fetch_center_inner(tex, uv);
}

void main(void)
{
    vec4 center = fetch_center(normal_map, pass_tex);

    FragColor = vec4(1.0);
    if (edge_exclude_get(center))   return;

    float depth_edge = laplace_float(model_tex, pass_tex, 3, near_plane, far_plane);
    depth_edge = max(depth_edge - 0.1, 0.0); // Excessive noise

    float normal_edge = sobel_filter_2d(normal_map, pass_tex);
    float mixed_edge = max(normal_edge, depth_edge);

    FragColor = vec4(vec3(1.0 - mixed_edge), 1.0);
}
