#ifndef SHADERS_SMAA_NEIGHBORHOOD_BLEND_GLSL
#define SHADERS_SMAA_NEIGHBORHOOD_BLEND_GLSL

#include "texel_fetch.glsl"

f16vec3 apply_edge(in vec3 color, in sampler2D edges, in float blend, in vec2 uv, in ivec2 off)
{
    float16_t factor = H(1.0 - texel_fetch_2d(edges, uv, off).r);
    return mix(HVEC3(color), HVEC3(0.0), factor * H(blend));
}

f16vec3 apply_edge(in sampler2D tex, in sampler2D edges, in float blend, in vec2 uv, in ivec2 off)
{
    return apply_edge(texel_fetch_2d(tex, uv, ivec2(0)).rgb, edges, blend, uv, off);
}

f16vec3 smaa_blend(in sampler2D tex, in sampler2D edges, in sampler2D smaa, in vec2 uv)
{
    f16vec3 color = apply_edge(tex, edges, H(1.0), uv, ivec2(0));
    f16vec4 blend = HVEC4(texel_fetch_2d(smaa, uv, ivec2(0)));

    /* blend.xyzw are 4 directions from which to blend */
    color += blend.x * apply_edge(tex, edges, blend.x, uv, ivec2(-1,  0));
    color += blend.y * apply_edge(tex, edges, blend.y, uv, ivec2( 1,  0));
    color += blend.z * apply_edge(tex, edges, blend.z, uv, ivec2( 0,  1));
    color += blend.w * apply_edge(tex, edges, blend.w, uv, ivec2( 0, -1));

    return color / (H(1.0) + blend.x + blend.y + blend.z + blend.w);
}

#endif /* SHADERS_SMAA_NEIGHBORHOOD_BLEND_GLSL */
