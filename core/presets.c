// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "light.h"
#include "mesh.h"
#include "model.h"
#include "noise.h"
#include "pipeline.h"
#include "presets.h"
#include "primitives.h"
#include "render.h"
#include "scene.h"
#include "shader.h"
#include "transform.h"

typedef struct shader_prog shader_prog;
DEFINE_CLEANUP(shader_prog, if (*p) ref_put(*p))

cres(noise_bg) noise_bg_new(struct scene *scene, const noise_bg_opts *opts)
{
    if (!scene || !opts)    return cres_error(noise_bg, CERR_INVALID_ARGUMENTS);

    pipeline *pl = clap_get_pipeline(scene->clap_ctx);
    if (!pl)    return cres_error(noise_bg, CERR_INVALID_OPERATION);

    LOCAL_SET(shader_prog, prog) = CRES_RET(pipeline_shader_find_get(pl, "model"),
                                            return cres_error(noise_bg, CERR_SHADER_NOT_LOADED));

    prim_emit_opts pr_opts = { .name = "noise-bg" };
    pr_opts.mesh = CRES_RET(_prim_begin(12, &pr_opts),
                            return cres_error(noise_bg, CERR_NOMEM));

    float e = opts->extent;
    vec3 corners[4] = {
        { opts->pos[0] - e, opts->pos[1], opts->pos[2] - e },
        { opts->pos[0] + e, opts->pos[1], opts->pos[2] - e },
        { opts->pos[0] + e, opts->pos[1], opts->pos[2] + e },
        { opts->pos[0] - e, opts->pos[1], opts->pos[2] + e },
    };
    prim_emit_quad(corners, .mesh = pr_opts.mesh);

    model3dtx *txm = CRES_RET(
        _prim_end_model3dtx(prog, opts->tex ? opts->tex : white_pixel(), &scene->mq, &pr_opts),
        { ref_put_last(pr_opts.mesh); return cres_error(noise_bg, CERR_NOMEM); }
    );
    ref_put_last(pr_opts.mesh);

    txm->mat.use_noise_emission  = true;
    vec3_dup(txm->mat.noise_emission_color, (float *)opts->emission);
    txm->mat.use_noise_normals   = NOISE_NORMALS_3D;
    txm->mat.noise_normals_scale = opts->noise_scale;
    txm->mat.noise_normals_amp   = opts->noise_amp;
    model3dtx_set_texture(txm, UNIFORM_NOISE3D_TEX,
                          noise3d_texture(clap_get_noise3d(scene->clap_ctx)));

    struct entity3d *ent = CRES_RET(ref_new_checked(entity3d, .txmodel = txm),
                                    return cres_error(noise_bg, CERR_NOMEM));
    entity3d_set(ent, ENTITY3D_SKIP_CULLING, NULL);
    entity3d_position(ent, (float *)opts->pos);
    ent->bloom_threshold = opts->bloom_threshold;
    ent->bloom_intensity = opts->bloom_intensity;

    noise_bg bg = { .entity = ent, .light_idx = -1 };

    if (opts->add_light) {
        cres(int) lres = light_get(&scene->light);
        if (IS_CERR(lres)) {
            ref_put(ent);
            return cres_error_cerr(noise_bg, cerr_error_cres(lres));
        }
        bg.light_idx = lres.val;
        light_set_pos(&scene->light, bg.light_idx, opts->light_pos);
        light_set_direction(&scene->light, bg.light_idx, (float *)opts->light_dir);
        light_set_directional(&scene->light, bg.light_idx, true);
        light_set_color(&scene->light, bg.light_idx, opts->light_color);
    }

    if (opts->reposition_camera) {
        transform_set_pos(&scene->camera->xform, opts->camera_pos);
        transform_set_angles(&scene->camera->xform, (float *)opts->camera_angles, true);
    }

    return cres_val(noise_bg, bg);
}

void noise_bg_done(struct scene *scene, noise_bg *bg)
{
    if (!bg)    return;
    if (bg->light_idx >= 0)
        light_put(&scene->light, bg->light_idx);
    if (bg->entity)
        ref_put(bg->entity);
    bg->entity    = NULL;
    bg->light_idx = -1;
}
