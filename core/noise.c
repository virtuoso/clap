// SPDX-License-Identifier: Apache-2.0
#include <complex.h>
#include <float.h>
#include <stdint.h>
#include "error.h"
#include "interp.h"
#include "memory.h"
#include "noise.h"
#include "render.h"
#include "shader_constants.h"

#include "kiss_fft.h"

static void fft2d_fwd(kiss_fft_cfg cfg, float *channel, kiss_fft_cpx out[FILM_GRAIN_SIZE][FILM_GRAIN_SIZE])
{
    kiss_fft_cpx row_in[FILM_GRAIN_SIZE], row_out[FILM_GRAIN_SIZE];

    /* rows */
    for (int y = 0; y < FILM_GRAIN_SIZE; y++) {
        for (int x = 0; x < FILM_GRAIN_SIZE; x++) {
            row_in[x].r = channel[y * FILM_GRAIN_SIZE + x];
            row_in[x].i = 0.0f;
        }

        kiss_fft(cfg, row_in, row_out);

        for (int x = 0; x < FILM_GRAIN_SIZE; x++)
            out[y][x] = row_out[x];
    }

    /* columns */
    kiss_fft_cpx col_in[FILM_GRAIN_SIZE], col_out[FILM_GRAIN_SIZE];
    for (int x = 0; x < FILM_GRAIN_SIZE; x++) {
        for (int y = 0; y < FILM_GRAIN_SIZE; y++)
            col_in[y] = out[y][x];

        kiss_fft(cfg, col_in, col_out);

        for (int y = 0; y < FILM_GRAIN_SIZE; y++)
            out[y][x] = col_out[y];
    }
}

static void fft2d_inv(kiss_fft_cfg cfg, kiss_fft_cpx in[FILM_GRAIN_SIZE][FILM_GRAIN_SIZE], float *channel)
{
    kiss_fft_cpx row_in[FILM_GRAIN_SIZE], row_out[FILM_GRAIN_SIZE];

    /* rows */
    for (int y = 0; y < FILM_GRAIN_SIZE; y++) {
        for (int x = 0; x < FILM_GRAIN_SIZE; x++)
            row_in[x] = in[y][x];

        kiss_fft(cfg, row_in, row_out);

        for (int x = 0; x < FILM_GRAIN_SIZE; x++)
            in[y][x] = row_out[x];
    }

    /* columns */
    kiss_fft_cpx col_in[FILM_GRAIN_SIZE], col_out[FILM_GRAIN_SIZE];
    for (int x = 0; x < FILM_GRAIN_SIZE; x++) {
        for (int y = 0; y < FILM_GRAIN_SIZE; y++)
            col_in[y] = in[y][x];

        kiss_fft(cfg, col_in, col_out);

        for (int y = 0; y < FILM_GRAIN_SIZE; y++)
            channel[y * FILM_GRAIN_SIZE + x] = col_out[y].r / (FILM_GRAIN_SIZE * FILM_GRAIN_SIZE);
    }
}

static void blue_noise2d_gain(kiss_fft_cpx buf[FILM_GRAIN_SIZE][FILM_GRAIN_SIZE])
{
    float maxr = sqrtf((FILM_GRAIN_SIZE / 2) * (FILM_GRAIN_SIZE / 2) + (FILM_GRAIN_SIZE / 2) * (FILM_GRAIN_SIZE / 2));

    for (int y = 0; y < FILM_GRAIN_SIZE; y++) {
        int fy = (y <= FILM_GRAIN_SIZE / 2) ? y : y - FILM_GRAIN_SIZE;

        for (int x = 0; x < FILM_GRAIN_SIZE; x++) {
            int fx = (x <= FILM_GRAIN_SIZE / 2) ? x : x - FILM_GRAIN_SIZE;

            float r = sqrt(fx * fx + fy * fy);
            float gain = r / maxr;

            buf[y][x].r *= gain;
            buf[y][x].i *= gain;
        }
    }
}

DEFINE_CLEANUP(float, if (*p) mem_free(*p))

cerr blue_noise2d_tex(texture_t *tex, int size)
{
    kiss_fft_cfg fft_fwd = kiss_fft_alloc(size, 0, NULL, NULL);
    kiss_fft_cfg fft_inv = kiss_fft_alloc(size, 1, NULL, NULL);

    LOCAL_SET(float, buf) = mem_alloc(4 * sizeof(float), .nr = size * size);
    if (!buf)   return CERR_NOMEM;

    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float r = ((drand48() * 4.0 - 1.0) / 3.0) * 0.299;
            float g = ((drand48() * 4.0 - 1.0) / 3.0) * 0.587;
            float b = ((drand48() * 4.0 - 1.0) / 3.0) * 0.114;
            buf[(x + y * size) * 4 + 0] = r;
            buf[(x + y * size) * 4 + 1] = g;
            buf[(x + y * size) * 4 + 2] = b;
            buf[(x + y * size) * 4 + 3] = 1.0;
        }

    float *chan = mem_alloc(sizeof(float), .nr = size * size);
    for (int c = 0; c < 3; c++) {
        // float chan[FILM_GRAIN_SIZE * FILM_GRAIN_SIZE];
        for (int i = 0; i < size * size; i++)
            chan[i] = buf[i * 4 + c];

        /* FFT forward */
        kiss_fft_cpx spectrum[FILM_GRAIN_SIZE][FILM_GRAIN_SIZE];
        fft2d_fwd(fft_fwd, chan, spectrum);

        /* sculpt */
        blue_noise2d_gain(spectrum);

        /* FFT inverse */
        // float chan_out[FILM_GRAIN_SIZE * FILM_GRAIN_SIZE];
        // fft2d_inv(fft_inv, spectrum, chan_out);
        fft2d_inv(fft_inv, spectrum, chan);

        for (int i = 0; i < size * size; i++)
            buf[i * 4 + c] = chan[i];
        // buf[i * 4 + c] = chan_out[i];
    }
    mem_free(chan);

    free(fft_fwd);
    free(fft_inv);

    float minv = INFINITY, maxv = -INFINITY;
    for (int i = 0; i < size * size * 4; i++) {
        if ((i & 3) == 3)   continue;

        if (buf[i] < minv)  minv = buf[i];
        if (buf[i] > maxv)  maxv = buf[i];
    }

    for (int i = 0; i < size * size * 4; i++)
        if ((i & 3) != 3)
            buf[i] = (buf[i] - minv) / (maxv - minv);

    CERR_RET(
        texture_init(
            tex,
            .type   = TEX_2D,
            .format = TEX_FMT_RGBA32F,
            .min_filter = TEX_FLT_NEAREST,
            .mag_filter = TEX_FLT_NEAREST,
            .wrap       = TEX_WRAP_REPEAT,
        ),
        return __cerr
    );

    CERR_RET(
        texture_load(tex, TEX_FMT_RGBA32F, size, size, buf),
        { texture_deinit(tex); return __cerr; }
    );

    return CERR_OK;
}

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
