// SPDX-License-Identifier: Apache-2.0
#include <float.h>
#include <stdint.h>
#include "error.h"
#include "interp.h"
#include "memory.h"
#include "noise.h"
#include "render.h"

static float value_noise3d_periodic(float x, float y, float z, int period, uint32_t seed)
{
    int xi0 = (int)floorf(x), yi0 = (int)floorf(y), zi0 = (int)floorf(z);
    float xf = x - xi0, yf = y - yi0, zf = z - zi0;
    int xi1 = xi0 + 1, yi1 = yi0 + 1, zi1 = zi0 + 1;

    /* wrap to period */
    xi0 = (xi0 % period + period) % period;
    yi0 = (yi0 % period + period) % period;
    zi0 = (zi0 % period + period) % period;
    xi1 = (xi1 % period + period) % period;
    yi1 = (yi1 % period + period) % period;
    zi1 = (zi1 % period + period) % period;

    float c000 = hash31(xi0, yi0, zi0, seed);
    float c100 = hash31(xi1, yi0, zi0, seed);
    float c010 = hash31(xi0, yi1, zi0, seed);
    float c110 = hash31(xi1, yi1, zi0, seed);
    float c001 = hash31(xi0, yi0, zi1, seed);
    float c101 = hash31(xi1, yi0, zi1, seed);
    float c011 = hash31(xi0, yi1, zi1, seed);
    float c111 = hash31(xi1, yi1, zi1, seed);

    float ux = smoothf(xf), uy = smoothf(yf), uz = smoothf(zf);
    float x00 = linf_interp(c000, c100, ux);
    float x10 = linf_interp(c010, c110, ux);
    float x01 = linf_interp(c001, c101, ux);
    float x11 = linf_interp(c011, c111, ux);
    float y0  = linf_interp(x00, x10, uy);
    float y1  = linf_interp(x01, x11, uy);
    return linf_interp(y0, y1, uz); /* ~[0..1] */
}

static float fbm3_periodic(float x, float y, float z,
                           int octaves, float lacunarity, float gain,
                           int period, uint32_t seed)
{
    float a = 0.5f, v = 0.0f;
    float fx = x, fy = y, fz = z;
    int p = period;
    for (uint32_t i = 0; i < octaves; i++) {
        v += value_noise3d_periodic(fx, fy, fz, p, seed + i) * a;
        fx *= lacunarity;
        fy *= lacunarity;
        fz *= lacunarity;
        p  = (int)lrintf((float)p * lacunarity); /* keep periodic, grows with freq */
        a *= gain;
    }
    return v;
}

cresp(void) noise_grad3d_bake_rgb8(size_t size, int octaves, float lacunarity, float gain,
                                   float period_units, uint32_t seed)
{
    /* coordinates cover [0, period_units) and wrap tileably */
    const float step = period_units / (float)size;
    const float eps  = step; /* central difference step */

    const size_t voxels = size * size * size;
    uint8_t *out = mem_alloc(voxels * 3);
    if (!out) return cresp_error(void, CERR_NOMEM);

    size_t idx = 0;
    for (size_t z = 0; z < size; z++) {
        float pz = z * step;
        for (size_t y = 0; y < size; y++) {
            float py = y * step;
            for (size_t x = 0; x < size; x++) {
                float px = x * step;

                /* central-difference gradient of periodic fBm */
                float fx1 = fbm3_periodic(px + eps, py, pz, octaves, lacunarity, gain, (int)period_units, seed);
                float fx0 = fbm3_periodic(px - eps, py, pz, octaves, lacunarity, gain, (int)period_units, seed);
                float fy1 = fbm3_periodic(px, py + eps, pz, octaves, lacunarity, gain, (int)period_units, seed);
                float fy0 = fbm3_periodic(px, py - eps, pz, octaves, lacunarity, gain, (int)period_units, seed);
                float fz1 = fbm3_periodic(px, py, pz + eps, octaves, lacunarity, gain, (int)period_units, seed);
                float fz0 = fbm3_periodic(px, py, pz - eps, octaves, lacunarity, gain, (int)period_units, seed);

                float gx = (fx1 - fx0) * (0.5f / eps);
                float gy = (fy1 - fy0) * (0.5f / eps);
                float gz = (fz1 - fz0) * (0.5f / eps);

                /* normalize; avoid zero */
                float len2 = gx * gx + gy * gy + gz * gz;
                float inv = 1.0f / sqrtf(len2 > FLT_MIN ? len2 : FLT_MIN);
                gx *= inv;
                gy *= inv;
                gz *= inv;

                /* pack to RGB8 in [0..255] */
                out[idx++] = (uint8_t)lrintf((gx * 0.5f + 0.5f) * 255.0f);
                out[idx++] = (uint8_t)lrintf((gy * 0.5f + 0.5f) * 255.0f);
                out[idx++] = (uint8_t)lrintf((gz * 0.5f + 0.5f) * 255.0f);
            }
        }
    }

    return cresp_val(void, out);
}

cerr noise_grad3d_bake_rgb8_tex(texture_t *tex, int size, int octaves, float lacunarity, float gain,
                                float period_units, uint32_t seed)
{
    LOCAL_SET(void, buf) = CRES_RET_CERR(
        noise_grad3d_bake_rgb8(size, octaves, lacunarity, gain, period_units, seed)
    );

    CERR_RET(
        texture_init(
            tex,
            .type   = TEX_3D,
            .format = TEX_FMT_RGB8,
            .layers = size,
            .min_filter = TEX_FLT_LINEAR,
            .mag_filter = TEX_FLT_LINEAR,
            .wrap       = TEX_WRAP_REPEAT,
        ),
        return __cerr
    );

    return texture_load(tex, TEX_FMT_RGB8, size, size, buf);
}
