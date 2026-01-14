#ifndef SHADERS_SMAA_NEIGHBORHOOD_BLEND_GLSL
#define SHADERS_SMAA_NEIGHBORHOOD_BLEND_GLSL

#include "texel_fetch.glsl"

vec3 apply_edge(in sampler2D tex, in sampler2D edges, in float blend, in vec2 uv, in ivec2 off)
{
    float factor = 1.0 - texel_fetch_2d(edges, uv, off).r;
    return mix(texel_fetch_2d(tex, uv, off).rgb, vec3(0.0), factor * blend);
}

vec3 smaa_blend(in sampler2D tex, in sampler2D edges, in sampler2D smaa, in vec2 uv)
{
    vec3 color = apply_edge(tex, edges, 1.0, uv, ivec2(0));
    vec4 blend = texel_fetch_2d(smaa, uv, ivec2(0));

    /* blend.xyzw are 4 directions from which to blend */
    color += blend.x * apply_edge(tex, edges, blend.x, uv, ivec2(-1,  0));
    color += blend.y * apply_edge(tex, edges, blend.y, uv, ivec2( 1,  0));
    color += blend.z * apply_edge(tex, edges, blend.z, uv, ivec2( 0,  1));
    color += blend.w * apply_edge(tex, edges, blend.w, uv, ivec2( 0, -1));

    return color / (1.0 + blend.x + blend.y + blend.z + blend.w);
}

#endif /* SHADERS_SMAA_NEIGHBORHOOD_BLEND_GLSL */
