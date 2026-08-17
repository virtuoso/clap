#ifndef SHADERS_VIEW_POS_GLSL
#define SHADERS_VIEW_POS_GLSL

#include "shader_constants.h"
#include "pass-tex.glsl"
#include "ndc-z.glsl"

vec2 convert_to_ndc_xy(in vec2 uv)
{
    return uv * 2.0 - 1.0;
}

/*
 * View space fragment's position reconstruction from an inverse projection
 * matrix, depth map and normalized coordinates in texture coordinate space
 * using texelFetch() for depth buffer textures: avoids requiring a filtering
 * sampler, which WebGPU disallows for Depth32Float textures.
 *
 * A texel nothing was rendered to holds the clear depth 1.0 and
 * reconstructs to its point on the far plane, like any other depth; a
 * caller that needs to tell the two apart reads @depth.
 */
vec3 view_pos_from_depth(in sampler2D depth_map, in mat4 inv_proj, in vec2 uv, out float depth)
{
    /*
     * texelFetch() has no wrap mode: a coordinate outside the map is
     * undefined. The uv is not always a pixel center of this pass: the
     * SSAO kernel projects its samples anywhere, including off screen,
     * the TAA resolve offsets to the nearest depth in its neighborhood
     * and the bloom downsample moves by the TAA jitter, all of which
     * cross the edge at the border pixels.
     */
    ivec2 size = textureSize(depth_map, 0);
    ivec2 tc = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
    depth = texelFetch(depth_map, tc, 0).r;

    // Assume depth map is 0.0 <= z <= 1.0
    vec4 ndc = vec4(convert_to_ndc_xy(convert_pass_tex(uv)), convert_to_ndc_z(depth), 1.0);
    vec4 view_pos = inv_proj * ndc;
    return view_pos.xyz / view_pos.w;
}

vec3 view_pos_from_depth(in sampler2D depth_map, in mat4 inv_proj, in vec2 uv)
{
    float depth;
    return view_pos_from_depth(depth_map, inv_proj, uv, depth);
}

#endif /* SHADERS_VIEW_POS_GLSL */
