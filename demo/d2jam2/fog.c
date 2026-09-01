// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "model.h"
#include "noise.h"
#include "particle.h"
#include "pipeline.h"
#include "render.h"
#include "scene.h"
#include "shader_constants.h"
#include "d2jam2.h"

static particle_system *fog;

cerr fog_init(struct scene *scene)
{
    pipeline *pl = clap_get_pipeline(scene->clap_ctx);
    struct shader_prog *prog = CRES_RET_CERR(pipeline_shader_find_get(pl, "particle"));

    fog = CRES_RET_CERR(
        ref_new_checked(
            particle_system,
            .name       = "fog",
            .prog       = ref_pass(prog),
            .mq         = &scene->mq,
            .dist       = PART_DIST_CBRT,
            .emit       = transparent_pixel(),
            .tex        = transparent_pixel(),
            .count      = 64,
            .radius     = 50.0,
            .min_radius = 10.0,
            .scale      = 2.0,
            .velocity   = 0.03,
        )
    );

    /* the quads carry no texture: the 3D fog in the lighting is all there is to them */
    entity3d *e = particle_system_entity(fog);
    material *mat = &e->txmodel->mat;

    mat->use_3d_fog         = true;
    mat->fog_3d_amp         = 0.7;
    mat->fog_3d_scale       = 0.05;
    mat->use_noise_normals  = NOISE_NORMALS_NONE;
    mat->metallic           = 0.0;
    mat->roughness          = 1.0;
    model3dtx_set_texture(e->txmodel, UNIFORM_NOISE3D_TEX, noise3d_texture(clap_get_noise3d(scene->clap_ctx)));
    e->txmodel->model->alpha_blend = true;

    return CERR_OK;
}

void fog_done(void)
{
    if (fog)    ref_put(fog);
    fog = NULL;
}
