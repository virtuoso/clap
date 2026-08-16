#ifndef SHADERS_BLOOM_LEVEL_GLSL
#define SHADERS_BLOOM_LEVEL_GLSL

#include "shader_constants.h"
#include "color-utils.glsl"

/*
 * The bloom chain carries the pyramid level its emitters want in the
 * alpha it has no other use for: premultiplied by luma, so that the
 * linear filters along the chain keep it a luma-weighted mean over the
 * emitters that lit a texel rather than over whatever the halo lands
 * on, and relative to the level it sits at, so that a mip step moves
 * it one level down by subtracting one luma and no level needs to know
 * its own index. Offset and scaled into [0, luma] so it survives a
 * UNORM target. The two-chain bloom reads the same value at its finest
 * level and lerps by it. BLOOM_LEVELS comes from shader_constants.h.
 */
#define BLOOM_LEVEL_RANGE   (float(BLOOM_LEVELS) - 1.0)

/* alpha for a target @rel levels above the one being written, at @luma */
float bloom_level_encode(float luma, float rel)
{
    return luma * (rel + BLOOM_LEVEL_RANGE) / (2.0 * BLOOM_LEVEL_RANGE);
}

/* alpha for the same target seen from one level down, off a downsampled texel */
float bloom_level_step(vec4 c)
{
    return c.a - luma(c.rgb) / (2.0 * BLOOM_LEVEL_RANGE);
}

/* the target level relative to this one */
float bloom_level_decode(vec4 c)
{
    return c.a / max(luma(c.rgb), 1e-6) * (2.0 * BLOOM_LEVEL_RANGE) - BLOOM_LEVEL_RANGE;
}

#endif /* SHADERS_BLOOM_LEVEL_GLSL */
