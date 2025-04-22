/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SSAO_H__
#define __CLAP_SSAO_H__

#include <stdbool.h>
#include "linmath.h"
#include "render.h"
#include "shader_constants.h"

typedef struct ssao_state {
    vec3        kernel[SSAO_KERNEL_SIZE];
    texture_t   noise;
    bool        initialized;
} ssao_state;

void ssao_init(ssao_state *ssao);
void ssao_done(ssao_state *ssao);
void ssao_upload(ssao_state *ssao, struct shader_prog *prog, unsigned int width, unsigned int height);

#endif /* __CLAP_SSAO_H__ */
