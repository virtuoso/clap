/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_NOISE_H__
#define __CLAP_NOISE_H__

#include <stdint.h>
#include "error.h"
#include "render.h"

static inline float hash31(int x, int y, int z, uint32_t seed)
{
    uint32_t h = (uint32_t)x * 374761393u
               + (uint32_t)y * 668265263u
               + (uint32_t)z * 362437u
               + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (h ^ (h >> 16)) * (1.0f / 4294967296.0f);
}

cresp(void) noise_grad3d_bake_rgba8(size_t size, int octaves, float lacunarity, float gain,
                                    float period_units, uint32_t seed);

cerr noise_grad3d_bake_rgba8_tex(renderer_t *r, texture_t *tex, int size, int octaves, float lacunarity,
                                 float gain, float period_units, uint32_t seed);

DEFINE_REFCLASS_INIT_OPTIONS(noise3d,
    texture_t   tex;
    renderer_t  *renderer;
    int         size;           /* side */
    int         octaves;        /* fBm octaves */
    float       lacunarity;     /* fBm lacunarity */
    float       gain;           /* fBm gain */
    float       period_units;   /* period */
    uint32_t    seed;           /* RNG seed */
);

DECLARE_REFCLASS(noise3d);
typedef struct noise3d noise3d;

texture_t *noise3d_texture(noise3d *n3d);

cerr blue_noise2d_tex(renderer_t *renderer, texture_t *tex, int size);

#endif /* __CLAP_NOISE_H__ */
