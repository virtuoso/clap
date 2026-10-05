// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "memory.h"
#include "mesh.h"
#include "model.h"
#include "noise.h"
#include "physics.h"
#include "pipeline.h"
#include "primitives.h"
#include "scene.h"
#include "pcg/commit.h"
#include "pcg/labels.h"

/**
 * struct pcg_built - a model made, waiting to join the scene
 * @txm:        the model, the creation reference
 * @mat:        the material it wears
 * @collide:    &enum pcg_collide of its surfaces
 */
struct pcg_built {
    model3dtx           *txm;
    const pcg_material  *mat;
    uint8_t             collide;
};

/**
 * struct pcg_commit - one commit in progress
 * @opts:   the commit's options
 * @built:  the models made so far; a model's slot empties when it joins
 *          the scene
 */
struct pcg_commit {
    const pcg_commit_opts       *opts;
    darray(struct pcg_built,    built);
};

static void pcg_commit_done(struct pcg_commit *c)
{
    struct pcg_built *b;

    darray_for_each(b, c->built)
        if (b->txm) ref_put(b->txm);
    darray_clearout(c->built);
}

typedef struct pcg_commit pcg_commit;
static DEFINE_CLEANUP_EXACT(pcg_commit, pcg_commit_done(p))

/* make the model of one mesh; it joins the scene later */
static cerr pcg_commit_mesh(const pcg_key *key, struct mesh *mesh, void *priv)
{
    pcg_commit *c = priv;
    const pcg_material *mat = key->mat;
    struct scene *scene = c->opts->scene;

    if (!mat->albedo)   return CERR_INVALID_ARGUMENTS_REASON(.fmt = "material '%s' has no albedo", .arg0 = mat->name);

    struct pcg_built *b = darray_add(c->built);
    if (!b)     return CERR_NOMEM;

    *b = (struct pcg_built){ .mat = mat, .collide = key->collide };

    /* nothing is appended after this */
    auto prog = CRES_RET_CERR(pipeline_shader_find_get(clap_get_pipeline(scene->clap_ctx), "model"));
    LOCAL_SET(model3d, mdl) = CRES_RET_CERR(prim_end_model3d(ref_pass(prog), .mesh = mesh));
    mdl->alpha_blend = mat->alpha_blend;
    mdl->skip_shadow = mat->skip_shadow;
    mdl->cull_face = mat->cull_face;

    model3dtx *txm = CRES_RET_CERR(ref_new_checked(model3dtx, .model = ref_pass(mdl), .tex = mat->albedo));
    b->txm = txm;

    txm->mat = mat->shade;
    if (mat->normal)    model3dtx_set_texture(txm, UNIFORM_NORMAL_MAP, mat->normal);
    if (mat->emission)  model3dtx_set_texture(txm, UNIFORM_EMISSION_MAP, mat->emission);
    model3dtx_set_texture(txm, UNIFORM_NOISE3D_TEX, noise3d_texture(clap_get_noise3d(scene->clap_ctx)));

    return CERR_OK;
}

/* a built model joins the scene, with its entity */
static cerr pcg_commit_place(pcg_commit *c, struct pcg_built *b)
{
    struct scene *scene = c->opts->scene;
    model3dtx *txm = b->txm;
    vec3 origin;

    /* the scene's queue takes over the creation reference */
    b->txm = nullptr;
    scene_add_model(scene, txm);

    entity3d *e = CRES_RET_CERR(ref_new_checked(entity3d, .txmodel = txm));

    vec3_dup(origin, c->opts->origin);
    entity3d_position(e, origin);
    if (c->opts->lod >= 0)  entity3d_set_lod(e, c->opts->lod, true);
    e->bloom_intensity = b->mat->bloom;

    if (b->collide == PCG_COLLIDE_NONE)     return CERR_OK;

    entity3d_add_physics(e, clap_get_phys(scene->clap_ctx), 0.0, GEOM_TRIMESH, PHYS_GEOM, 0.0, 0.0, 0.0);
    if (!entity3d_matches(e, ENTITY3D_HAS_PHYSICS)) {
        entity3d_delete(e);
        return CERR_INITIALIZATION_FAILED_REASON(.fmt = "no collider for '%s'", .arg0 = txm->model->name);
    }

    if (b->collide == PCG_COLLIDE_HAZARD)   entity3d_set(e, ENTITY3D_NM_HAZARD, nullptr);

    return CERR_OK;
}

cerr _pcg_sink_commit(pcg_sink *s, const pcg_commit_opts *opts)
{
    if (!s || !opts || !opts->scene || !opts->lib)  return CERR_INVALID_ARGUMENTS;
    if (opts->lod < -1)
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "LOD %zd below -1", .arg0 = (void *)(intptr_t)opts->lod);

    LOCAL_SET_EXACT(pcg_commit, c) = { .opts = opts };
    darray_init(c.built);

    /* every model first: a failure here leaves the scene alone */
    CERR_RET_CERR(pcg_sink_for_each_mesh(s, pcg_commit_mesh, &c));
    CERR_RET_CERR(pcg_labels_build(s, opts, pcg_commit_mesh, &c));

    /* models draw in the order they were added: blended ones last */
    for (int blended = 0; blended < 2; blended++) {
        struct pcg_built *b;

        darray_for_each(b, c.built)
            if (b->mat->alpha_blend == blended)     CERR_RET_CERR(pcg_commit_place(&c, b));
    }

    return CERR_OK;
}
