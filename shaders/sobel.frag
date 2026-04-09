#version 460 core

#include "shader_constants.h"

layout (location=0) in vec2 pass_tex;
layout (binding=SAMPLER_BINDING_depth_tex) uniform sampler2D depth_tex;
layout (binding=SAMPLER_BINDING_normal_map) uniform sampler2D normal_map;

layout (location=0) out vec4 FragColor;

#include "shader_constants.h"
#include "edge_filter.glsl"
#include "ubo_postproc.glsl"

void main(void)
{
    vec4 center = texture(normal_map, pass_tex);

    FragColor = vec4(1.0);
    if (edge_exclude_get(center))   return;

    float depth_edge = laplace_depth_fetch(depth_tex, pass_tex, 3, near_plane, far_plane);
    depth_edge = max(depth_edge - 0.1, 0.0); // Excessive noise

    vec3 normal_sobel = sobel_filter_2d(normal_map, pass_tex, center);
    float normal_edge = length(normal_sobel);
    float mixed_edge = max(normal_edge, depth_edge);

    FragColor = vec4(vec3(1 - mixed_edge), 1.0);
}
