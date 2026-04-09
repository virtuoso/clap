#ifndef SHADERS_VIEW_POS_GLSL
#define SHADERS_VIEW_POS_GLSL

#include "shader_constants.h"
#include "pass-tex.glsl"
#include "ndc-z.glsl"

vec2 convert_to_ndc_xy(in vec2 uv)
{
    return uv * 2.0 - 1.0;
}

// View space fragment's position reconstruction from an inverse projection
// matrix, depth map and normalized coordinates in texture coordinate space
// using texelFetch() for depth buffer textures: avoids requiring a filtering
// sampler, which WebGPU disallows for Depth32Float textures.
vec3 view_pos_from_depth(in sampler2D depth_map, in mat4 inv_proj, in vec2 uv)
{
    ivec2 tc = ivec2(uv * vec2(textureSize(depth_map, 0)));
    float depth = texelFetch(depth_map, tc, 0).r;
    if (depth >= 1.0)   return vec3(0.0, 0.0, 1.0);

    // Assume depth map is 0.0 <= z <= 1.0
    vec4 ndc = vec4(convert_to_ndc_xy(convert_pass_tex(uv)), convert_to_ndc_z(depth), 1.0);
    vec4 view_pos = inv_proj * ndc;
    return view_pos.xyz / view_pos.w;
}

#endif /* SHADERS_VIEW_POS_GLSL */
