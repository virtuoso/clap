/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_NOISE_H__
#define __CLAP_NOISE_H__

#include "linmath.h"
#include <stdint.h>
#include "error.h"
#include "render.h"

static inline float hash3i(int x, int y, int z, uint32_t seed)
{
    // int _x = x * 3183099 + 1000000;
    // int _y = y * 3183099 + 2000000;
    // int _z = z * 3183099 + 3000000;
    vec3 p = { x, y, z };
    vec3_scale(p, p, 0.3183099);
    vec3_add(p, p, (vec3) { 0.1, 0.2, 0.3 });
    // p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
    vec3_scale(p, p, 17.0);
    // p *= 17.0;
    float ret = p[0] * p[1] * p[2] * (p[0] + p[1] + p[2]);
    // return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
    return ret - floorf(ret);
}

static inline float hash31(int x, int y, int z, uint32_t seed)
{
    uint32_t h = (uint32_t)x * 374761393u
               + (uint32_t)y * 668265263u
               + (uint32_t)z * 362437u
               + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (h ^ (h >> 16)) * (1.0f / 4294967296.0f);
}

cresp(void) noise_grad3d_bake_rgb8(size_t size, int octaves, float lacunarity, float gain,
                                   float period_units, uint32_t seed);

cerr noise_grad3d_bake_rgb8_tex(texture_t *tex, int size, int octaves, float lacunarity, float gain,
                                float period_units, uint32_t seed);

cerr blue_noise2d_tex(texture_t *tex, int size);

#endif /* __CLAP_NOISE_H__ */
