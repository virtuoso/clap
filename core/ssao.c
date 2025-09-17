// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include "interp.h"
#include "shader.h"
#include "ssao.h"

static void ssao_kernel_init(ssao_state *ssao)
{
    for (int i = 0; i < SSAO_KERNEL_SIZE; i++) {
        vec3 sample = { drand48() * 2.0 - 1.0, drand48() * 2.0 - 1.0, drand48() };
        vec3_norm_safe(sample, sample);

        float scale = (float)i / SSAO_KERNEL_SIZE;
        scale = linf_interp(0.1f, 1.0f, scale * scale);
        vec3_scale(sample, sample, scale);
        vec3_dup(ssao->kernel[i], sample);
    }
}

static cerr ssao_noise_init(ssao_state *ssao)
{
    vec4 noise[SSAO_NOISE_DIM * SSAO_NOISE_DIM];

    for (int i = 0; i < SSAO_NOISE_DIM * SSAO_NOISE_DIM; i++) {
        vec4_dup(noise[i], (vec4){ drand48() * 2.0 - 1.0, drand48() * 2.0 - 1.0, 0.0, 0.0 });
        vec4_norm_safe(noise[i], noise[i]);
    }

    cerr err = texture_init(&ssao->noise,
        .wrap       = TEX_WRAP_REPEAT,
        .min_filter = TEX_FLT_NEAREST,
        .mag_filter = TEX_FLT_NEAREST,
    );
    if (IS_CERR(err))
        return err;

    err = texture_load(&ssao->noise, TEX_FMT_RGBA32F, SSAO_NOISE_DIM, SSAO_NOISE_DIM, noise);
    if (IS_CERR(err)) {
        texture_deinit(&ssao->noise);
        return err;
    }

    return CERR_OK;
}

void ssao_upload(ssao_state *ssao, struct shader_prog *prog, unsigned int width, unsigned int height)
{
    if (!ssao->initialized)
        return;

    shader_set_var_ptr(prog, UNIFORM_SSAO_KERNEL, SSAO_KERNEL_SIZE, ssao->kernel);
    vec2 noise_scale = { (float)width / SSAO_NOISE_DIM, (float)height / SSAO_NOISE_DIM };
    shader_set_var_ptr(prog, UNIFORM_SSAO_NOISE_SCALE, 1, noise_scale);
}

void ssao_init(ssao_state *ssao)
{
    if (ssao->initialized)
        return;

    cerr err = ssao_noise_init(ssao);
    if (IS_CERR(err)) {
        err_cerr(err, "couldn't initialize SSAO noise texture\n");
        return;
    }

    ssao_kernel_init(ssao);
    ssao->initialized = true;
}

void ssao_done(ssao_state *ssao)
{
    if (!ssao->initialized)
        return;

    texture_deinit(&ssao->noise);
    ssao->initialized = false;
}
