#ifndef SOBEL_FILTER_GLSL
#define SOBEL_FILTER_GLSL

#include "texel_fetch.glsl"

float linearize_depth(float depth, float near_plane, float far_plane)
{
    float linear_depth = near_plane * far_plane / (far_plane + depth * (near_plane - far_plane));
    return (linear_depth - near_plane) / (far_plane - near_plane) * 500.0;
}

float linearize_depth(vec3 pixel, float near_plane, float far_plane)
{
    float linear_depth = near_plane * far_plane / (far_plane + length(pixel) * (near_plane - far_plane));
    return (linear_depth - near_plane) / (far_plane - near_plane) * 500.0;
}

float depth_linear(sampler2D map, vec2 uv, ivec2 off, float near_plane, float far_plane)
{
    return linearize_depth(texel_fetch_2d(map, uv, off).r, near_plane, far_plane);
}

vec3 normals_fetch(sampler2D tex, vec2 tex_coords, ivec2 off)
{
    vec4 texel = texel_fetch_2d(tex, tex_coords, off);

    return texel.xyz / texel.w;
}

float normals_fetch(sampler2DMS tex, vec2 tex_coords)
{
    vec4 texel = texel_fetch_2dms(tex, tex_coords);

    return dot(texel.xyz / texel.w, normalize(vec3(0.299, 0.587, 0.114)) / texel.w); // Grayscale;
}

float laplace_float(sampler2D normals, vec2 tex_coords, int kernel)
{
    int side = (kernel - 1) / 2;
    vec3 sum = kernel * 2 * normals_fetch(normals, tex_coords, ivec2(0)).rgb;

    for (int x = -side; x <= side; x++)
        sum -= normals_fetch(normals, tex_coords, ivec2(x, 0)).rgb;
    for (int y = -side; y <= side; y++)
        sum -= normals_fetch(normals, tex_coords, ivec2(0, y)).rgb;

    return length(sum);
}

float laplace_float(sampler2D depths, vec2 tex_coords, int kernel, float near_plane, float far_plane)
{
    int side = (kernel - 1) / 2;
    vec3 sum = kernel * 2 * vec3(depth_linear(depths, tex_coords, ivec2(0), near_plane, far_plane));

    for (int x = -side; x <= side; x++)
        sum -= vec3(depth_linear(depths, tex_coords, ivec2(x, 0), near_plane, far_plane));
    for (int y = -side; y <= side; y++)
        sum -= vec3(depth_linear(depths, tex_coords, ivec2(0, y), near_plane, far_plane));

    return length(sum);
}

vec3 sobel_filter_2d(sampler2D tex, vec2 tex_coords)
{
    vec3 tl = normals_fetch(tex, tex_coords, ivec2(-1,  1)).rgb;
    vec3 tr = normals_fetch(tex, tex_coords, ivec2( 1,  1)).rgb;
    vec3 bl = normals_fetch(tex, tex_coords, ivec2(-1, -1)).rgb;
    vec3 br = normals_fetch(tex, tex_coords, ivec2( 1, -1)).rgb;

    vec3 gx = tr + 2.0 * normals_fetch(tex, tex_coords, ivec2( 1, 0)).rgb + br
            - (tl + 2.0 * normals_fetch(tex, tex_coords, ivec2(-1, 0)).rgb + bl);

    vec3 gy = bl + 2.0 * normals_fetch(tex, tex_coords, ivec2( 0, -1)).rgb + br
            - (tl + 2.0 * normals_fetch(tex, tex_coords, ivec2( 0,  1)).rgb + tr);

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

