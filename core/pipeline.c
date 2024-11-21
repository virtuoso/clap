// SPDX-License-Identifier: Apache-2.0
#include "model.h"
#include "pipeline.h"
#include "scene.h"
#include "shader.h"

struct render_pass {
    darray(struct render_pass *, src);
    darray(struct fbo *, fbo);
    darray(int, blit_src);
    struct mq           mq;
    struct list         entry;
    struct render_pass  *repeat;
    const char          *name;
    int                 rep_total;
    int                 rep_count;
    bool                blit;
};

static void pipeline_drop(struct ref *ref)
{
    struct pipeline *pl = container_of(ref, struct pipeline, ref);
    struct render_pass *pass, *iter;

    /*
     * We need 2 passes, because @pass sources reference elements behind itself
     * in the list, and in order to undo this relationship, we need the source
     * element to not be freed
     */
    list_for_each_entry(pass, &pl->passes, entry) {
        struct model3dtx *txm = mq_model_first(&pass->mq);
        int i;

        for (i = 0; i < darray_count(pass->fbo); i++) {
            struct fbo **pfbo = &pass->fbo.x[i];
            struct render_pass **psrc = &pass->src.x[i];

            if (txm->emission == &(*pfbo)->tex)
                txm->emission = &txm->_emission;
            if (txm->sobel == &(*pfbo)->tex)
                txm->sobel = &txm->_sobel;
        }

        darray_clearout(&pass->src.da);
        darray_clearout(&pass->blit_src.da);
    }

    list_for_each_entry_iter(pass, iter, &pl->passes, entry) {
        mq_release(&pass->mq);

        struct fbo **pfbo;
        darray_for_each(pfbo, &pass->fbo)
            ref_put(*pfbo);
        darray_clearout(&pass->fbo.da);

        free(pass);
    }
}
DECLARE_REFCLASS(pipeline);

struct pipeline *pipeline_new(struct scene *s, const char *name)
{
    struct pipeline *pl;

    CHECK(pl = ref_new(pipeline));
    list_init(&pl->passes);
    pl->scene = s;
    pl->name = name;

    return pl;
}

void pipeline_resize(struct pipeline *pl)
{
    struct render_pass *pass;

    list_for_each_entry(pass, &pl->passes, entry) {
        struct fbo **pfbo;
        darray_for_each(pfbo, &pass->fbo)
            fbo_resize(*pfbo, pl->scene->width, pl->scene->height);
    }
}

struct render_pass *pipeline_add_pass(struct pipeline *pl, struct render_pass *src, const char *prog_name,
                                      bool ms, int nr_targets, int target)
{
    struct render_pass *pass;
    struct shader_prog *p;
    struct model3dtx *txm;
    struct entity3d *e;
    struct model3d *m;

    CHECK(pass = calloc(1, sizeof(*pass)));
    list_append(&pl->passes, &pass->entry);
    darray_init(&pass->src);
    darray_init(&pass->fbo);
    darray_init(&pass->blit_src);
    pass->name = prog_name;

    /*
     * FBOs and srcs are a 1:1 mapping *except* the ms passes, which don't have
     * sources, and the data is copied out to following passes' textures instead
     * of by rendering a quad
     */
    struct fbo **pfbo = darray_add(&pass->fbo.da);
    if (!pfbo)
        return NULL;

    *pfbo = fbo_new_ms(pl->scene->width, pl->scene->height, ms, nr_targets);
    if (!*pfbo)
        goto err_fbo_array;

    struct render_pass **psrc = darray_add(&pass->src.da);
    if (!psrc)
        goto err_fbo;

    *psrc = src;

    pass->blit = ms;
    mq_init(&pass->mq, NULL);

    /*
     * XXX: any number of things mean the same thing: !prog_name, ms, nr_targets,
     * !src. Streamline the parameters of this function, it's a mess.
     */
    if (!prog_name)
        return pass;

    int *pblit_src = darray_add(&pass->blit_src.da);
    if (!pblit_src)
        goto err_src;

    *pblit_src = target;

    p = shader_prog_find(&pl->scene->shaders, prog_name);
    if (!p)
        goto err_blit_src;

    m = model3d_new_quad(p, -1, 1, 0, 2, -2);
    m->cull_face = true;
    m->debug = true;
    m->alpha_blend = false;
    txm = model3dtx_new_texture(ref_pass(m), &(*pfbo)->tex);
    mq_add_model(&pass->mq, txm);
    e = entity3d_new(txm);
    list_append(&txm->entities, &e->entry);
    e->visible = true;
    mat4x4_identity(e->mx->m);
    ref_put(p);

    return pass;

err_blit_src:
    darray_clearout(&pass->blit_src.da);
err_src:
    darray_clearout(&pass->src.da);
err_fbo:
    ref_put_last(*pfbo);
err_fbo_array:
    darray_clearout(&pass->fbo.da);
    free(pass);
    return NULL;
}

void pipeline_pass_repeat(struct render_pass *pass, struct render_pass *repeat, int count)
{
    pass->repeat = repeat;
    pass->rep_total = count;
}

void pipeline_pass_set_name(struct render_pass *pass, const char *name)
{
    pass->name = name;
}

void pipeline_pass_add_source(struct pipeline *pl, struct render_pass *pass, int to, struct render_pass *src, int blit_src)
{
    struct model3dtx *txmsrc = mq_model_first(&src->mq);
    struct model3dtx *txm = mq_model_first(&pass->mq);
    struct render_pass **psrc = darray_add(&pass->src.da);

    if (!psrc)
        return;

    struct fbo **pfbo = darray_add(&pass->fbo.da);
    if (!pfbo)
        goto err_src;

    /* this is where src's txmodel will be rendered */
    *pfbo = fbo_new_ms(pl->scene->width, pl->scene->height, false, 0);
    if (!*pfbo)
        goto err_fbo_array;

    int *pblit_src = darray_add(&pass->blit_src.da);
    if (!pblit_src)
        goto err_fbo;

    if (src->blit)
        *pblit_src = blit_src;

    *psrc = src;
    model3dtx_set_texture(txm, to, &(*pfbo)->tex);
    return;

err_fbo:
    ref_put(*pfbo);
err_fbo_array:
    darray_delete(&pass->fbo.da, -1);
err_src:
    darray_delete(&pass->src.da, -1);
}

void pipeline_render(struct pipeline *pl)
{
    struct scene *s = pl->scene;
    struct render_pass *last_pass = list_last_entry(&pl->passes, struct render_pass, entry);
    struct render_pass *pass, *ppass = NULL;
    bool repeating = false;

    list_for_each_entry(pass, &pl->passes, entry) {
        if (!ppass || !repeating || ppass->rep_count)
            ppass = darray_count(pass->src) ? pass->src.x[0] : NULL;

        int i;
        /*
         * This renders the contents of @ppass, using its shader into
         * @pass texture.
         */
repeat:
        /*
         * If by some reason a repeat pass ends up having more than one
         * source, only the first one will be sourced from the previous
         * pass in the loop, the rest still get rendered/blitted from
         * their original sources.
         */
        for (i = 0; i < darray_count(pass->src); i++, ppass = NULL) {
            struct render_pass *src = ppass ? ppass : pass->src.x[i];
            struct fbo *fbo = pass->fbo.x[i];

            if (src && src->blit) {
                struct fbo *src_fbo = src->fbo.x[0];
                fbo_prepare(fbo);
                GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo->fbo));
                GL(glBindFramebuffer(GL_READ_FRAMEBUFFER, src_fbo->fbo));
                GL(glReadBuffer(GL_COLOR_ATTACHMENT0 + pass->blit_src.x[i]));
                GL(glBlitFramebuffer(0, 0, src_fbo->width, src_fbo->height,
                                     0, 0, fbo->width, fbo->height,
                                     GL_COLOR_BUFFER_BIT, GL_LINEAR));
                fbo_done(fbo, s->width, s->height);
            } else {
                fbo_prepare(fbo);
                GL(glDisable(GL_DEPTH_TEST));
                GL(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
                GL(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));
                if (!src)
                    models_render(&s->mq, &s->light, &s->cameras[0], s->proj_mx, s->focus, s->width, s->height, NULL);
                else
                    models_render(&src->mq, NULL, NULL, NULL, NULL, s->width, s->height, NULL);

                fbo_done(fbo, s->width, s->height);
            }
        }

        if (pass->rep_count && pass->rep_count--) {
            if (!pass->rep_count) {
                ppass = NULL;
                repeating = false;
                continue;
            }

            ppass = pass;
            pass = pass->repeat;
            goto repeat;
        } else if (pass->repeat) {
            repeating = true;
            pass->rep_count = pass->rep_total;
            ppass = pass;
            pass = pass->repeat;
            goto repeat;
        }
    }

    /* render the last pass to the screen */
    GL(glEnable(GL_DEPTH_TEST));
    GL(glClearColor(0.2f, 0.2f, 0.6f, 1.0f));
    GL(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));
    models_render(&last_pass->mq, NULL, NULL, NULL, NULL, s->width, s->height, NULL);
}
