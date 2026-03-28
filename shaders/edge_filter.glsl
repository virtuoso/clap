#ifndef SOBEL_FILTER_GLSL
#define SOBEL_FILTER_GLSL

#include "texel_fetch.glsl"
#include "linearize-depth.glsl"
#include "half.glsl"

float depth_linear(sampler2D map, vec2 uv, ivec2 off, float near_plane, float far_plane)
{
    return linearize_depth(texel_fetch_2d(map, uv, off).r, near_plane, far_plane);
}

// Extract edge_mask from texel's alpha channel, assuming RGBA8 format
uint edge_mask_get(f16vec4 texel)
{
    // Assumes RGBA8 unorm
    return uint(texel.a * 255.0 + 0.5);
}

// Get outline_exclude bit's value from texel's alpha channel directly
bool edge_exclude_get(vec4 texel)
{
    return (edge_mask_get(HVEC4(texel)) & EDGE_EXCLUDE) != 0;
}

// Get solid body id from edge mask (EDGE_SOLID_LUMA_OFFSET bits);
// these use encoded luma values instead of normal vectors for edge detection
uint edge_solid_id_get(uint edge_mask)
{
    return edge_mask & EDGE_SOLID_MASK;
}

// Get luma value from edge_mask (EDGE_LUMA_WIDTH bits)
float16_t edge_luma_get(uint edge_mask)
{
    uint luma_packed = (edge_mask >> (EDGE_SOLID_LUMA_OFFSET)) & EDGE_LUMA_MAX;
    return H(luma_packed) * (H(256) / H(EDGE_LUMA_MAX + 1));
}

f16vec3 normals_fetch(sampler2D tex, vec2 tex_coords, ivec2 off, vec4 center)
{
    f16vec4 texel = off == ivec2(0) ? HVEC4(center) : HVEC4(texel_fetch_2d(tex, tex_coords, off));

    uint edge_mask = edge_mask_get(texel);
    uint edge_solid_id = edge_solid_id_get(edge_mask);
    float16_t luma = edge_luma_get(edge_mask);

    if (edge_solid_id != 0) {
        texel = HVEC4(luma);
        return texel.rgb * H(edge_solid_id);
    }

    if (luma > 0.0) return texel.rgb * luma;

    return texel.rgb;
}

float normals_fetch(sampler2DMS tex, vec2 tex_coords)
{
    vec4 texel = texel_fetch_2dms(tex, tex_coords);

    return dot(texel.xyz / texel.w, normalize(vec3(0.299, 0.587, 0.114)) / texel.w); // Grayscale;
}

float laplace_float(sampler2D normals, vec2 tex_coords, int kernel, vec4 center)
{
    int side = (kernel - 1) / 2;
    f16vec3 sum = H(kernel * 2) * normals_fetch(normals, tex_coords, ivec2(0), center).rgb;

    for (int x = -side; x <= side; x++)
        sum -= normals_fetch(normals, tex_coords, ivec2(x, 0), center).rgb;
    for (int y = -side; y <= side; y++)
        sum -= normals_fetch(normals, tex_coords, ivec2(0, y), center).rgb;

    return length(sum);
}

float laplace_float(sampler2D depths, vec2 tex_coords, int kernel, float near_plane, float far_plane)
{
    int side = (kernel - 1) / 2;
    float sum = kernel * 2 * depth_linear(depths, tex_coords, ivec2(0), near_plane, far_plane);

    for (int x = -side; x <= side; x++)
        sum -= depth_linear(depths, tex_coords, ivec2(x, 0), near_plane, far_plane);
    for (int y = -side; y <= side; y++)
        sum -= depth_linear(depths, tex_coords, ivec2(0, y), near_plane, far_plane);

    return clamp(abs(sum), 0.0, 1.0);
}

vec3 sobel_filter_2d(sampler2D tex, vec2 tex_coords, vec4 center)
{
    f16vec3 tl = normals_fetch(tex, tex_coords, ivec2(-1,  1), center).rgb;
    f16vec3 tr = normals_fetch(tex, tex_coords, ivec2( 1,  1), center).rgb;
    f16vec3 bl = normals_fetch(tex, tex_coords, ivec2(-1, -1), center).rgb;
    f16vec3 br = normals_fetch(tex, tex_coords, ivec2( 1, -1), center).rgb;

    f16vec3 gx = tr + H(2.0) * normals_fetch(tex, tex_coords, ivec2( 1, 0), center).rgb + br
            - (tl + H(2.0) * normals_fetch(tex, tex_coords, ivec2(-1, 0), center).rgb + bl);

    f16vec3 gy = bl + H(2.0) * normals_fetch(tex, tex_coords, ivec2( 0, -1), center).rgb + br
            - (tl + H(2.0) * normals_fetch(tex, tex_coords, ivec2( 0,  1), center).rgb + tr);

    return sqrt(gx * gx + gy * gy);
}

float sobel_filter_depth(sampler2D tex, vec2 tex_coords, float near_plane, float far_plane)
{
    vec3 tl = vec3(depth_linear(tex, tex_coords, ivec2(-1,  1), near_plane, far_plane));
    vec3 tr = vec3(depth_linear(tex, tex_coords, ivec2( 1,  1), near_plane, far_plane));
    vec3 bl = vec3(depth_linear(tex, tex_coords, ivec2(-1, -1), near_plane, far_plane));
    vec3 br = vec3(depth_linear(tex, tex_coords, ivec2( 1, -1), near_plane, far_plane));

    vec3 gx = tr + 2.0 * vec3(depth_linear(tex, tex_coords, ivec2( 1, 0), near_plane, far_plane)) + br
            - (tl + 2.0 * vec3(depth_linear(tex, tex_coords, ivec2(-1, 0), near_plane, far_plane)) + bl);

    vec3 gy = bl + 2.0 * vec3(depth_linear(tex, tex_coords, ivec2( 0, -1), near_plane, far_plane)) + br
            - (tl + 2.0 * vec3(depth_linear(tex, tex_coords, ivec2( 0,  1), near_plane, far_plane)) + tr);

    return length(gx * gx + gy * gy);
}

#endif

