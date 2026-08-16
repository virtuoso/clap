#version 460 core

#include "shader_constants.h"
#include "bloom-level.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

/* this level's downsample */
layout (binding=SAMPLER_BINDING_model_tex) uniform sampler2D model_tex;
/* the coarser levels' result so far; a black pixel at the coarsest */
layout (binding=SAMPLER_BINDING_bloom_tex) uniform sampler2D bloom_tex;

/*
 * 5x5 binomial over this level as 3x3 bilinear taps: the +-1 and +-2
 * texel weights (4 and 1)/16 fold into one tap at +-1.2 texels of
 * weight 5/16. The finest level is magnified 2x by combine_color() and
 * needs this to stay smooth; the coarser ones would be smooth from the
 * tent upsample alone
 */
vec4 binomial5(sampler2D tex, vec2 uv)
{
    vec2 ts = 1.0 / vec2(textureSize(tex, 0));
    const float w0 = 6.0 / 16.0, w1 = 5.0 / 16.0, o = 1.2;

    return texture(tex, uv) * (w0 * w0) +
           (texture(tex, uv + vec2(-o, 0.0) * ts) + texture(tex, uv + vec2(o, 0.0) * ts) +
            texture(tex, uv + vec2(0.0, -o) * ts) + texture(tex, uv + vec2(0.0, o) * ts)) * (w0 * w1) +
           (texture(tex, uv + vec2(-o, -o) * ts) + texture(tex, uv + vec2(o, -o) * ts) +
            texture(tex, uv + vec2(-o, o) * ts) + texture(tex, uv + vec2(o, o) * ts)) * (w1 * w1);
}

/* 3x3 tent over the coarser level as 4 bilinear taps at its half-texel corners */
vec3 tent_upsample(sampler2D tex, vec2 uv)
{
    vec2 hts = 0.5 / vec2(textureSize(tex, 0));

    return (texture(tex, uv + vec2(-hts.x, -hts.y)).rgb + texture(tex, uv + vec2(hts.x, -hts.y)).rgb +
            texture(tex, uv + vec2(-hts.x,  hts.y)).rgb + texture(tex, uv + vec2(hts.x,  hts.y)).rgb) * 0.25;
}

/*
 * One bloom pyramid level up: this level's own contribution, weighted
 * by how much of the emitters' target level it is (a tent over the
 * carried target minus this level, so the two levels around the target
 * split it and the shares sum to one), plus the coarser levels' so far
 */
void main()
{
    vec4 own = binomial5(model_tex, pass_tex);
    float weight = max(1.0 - abs(bloom_level_decode(own)), 0.0);

    FragColor = vec4(own.rgb * weight + tent_upsample(bloom_tex, pass_tex), 0.0);
}
