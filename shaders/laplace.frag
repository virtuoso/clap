#version 460 core

#include "shader_constants.h"
#include "texel_fetch.glsl"
#include "edge_filter.glsl"

layout (location=0) in vec2 pass_tex;
layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
layout (binding=SAMPLER_BINDING_normal_map) uniform sampler2D normal_map;

#include "ubo_postproc.glsl"

layout (location=0) out vec4 FragColor;

void main(void)
{
    float laplacian_normal_edge = laplace_float(normal_map, pass_tex, laplace_kernel);
    float laplacian_depth_edge = laplace_float(model_tex, pass_tex, laplace_kernel,
                                               near_plane, far_plane);
    laplacian_depth_edge = max(laplacian_depth_edge - 0.1, 0.0); // Excessive noise

    float mixed_edge = max(laplacian_normal_edge, laplacian_depth_edge);

    FragColor = vec4(1 - mixed_edge, 0.0, 0.0, 1.0);
}
