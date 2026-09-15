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

/**
 * value_noise2d_periodic() - tileable 2D value noise on an integer lattice
 * @x:          X coordinate
 * @y:          Y coordinate
 * @period_x:   X period, lattice cells; it may differ from @period_y for
 *              anisotropic tiles (wood grain and the like)
 * @period_y:   Y period, lattice cells
 * @seed:       noise seed
 *
 * Return: the noise at (@x, @y), ~[0..1]
 */
float value_noise2d_periodic(float x, float y, int period_x, int period_y, uint32_t seed);

/**
 * fbm2_periodic() - tileable 2D fractal value noise
 * @x:          X coordinate
 * @y:          Y coordinate
 * @octaves:    number of octaves
 * @lacunarity: frequency multiplier per octave
 * @gain:       amplitude multiplier per octave
 * @period_x:   X period of the first octave, lattice cells
 * @period_y:   Y period of the first octave, lattice cells
 * @seed:       noise seed
 *
 * Each octave's period is the first octave's times its frequency,
 * rounded, so the sum tiles where those products come out whole.
 * Return: the noise at (@x, @y), ~[0..1]
 */
float fbm2_periodic(float x, float y, int octaves, float lacunarity, float gain,
                    int period_x, int period_y, uint32_t seed);

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
