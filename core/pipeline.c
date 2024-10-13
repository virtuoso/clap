// SPDX-License-Identifier: Apache-2.0
#include "model.h"
#include "pipeline.h"
#include "scene.h"
#include "shader.h"

struct render_pass {
    darray(struct render_pass *, src);
    struct fbo          *fbo;
    struct mq           mq;
    struct list         entry;
    struct render_pass  *repeat;
    int                 rep_total;
    int                 rep_count;
    bool                blit;
    int                 blit_src;
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
        struct model3dtx *txm = list_first_entry(&pass->mq.txmodels, struct model3dtx, entry);
        struct render_pass **psrc;

        darray_for_each(psrc, &pass->src)
            if (txm->emission == &(*psrc)->fbo->tex)
                txm->emission = &txm->_emission;

        darray_clearout(&pass->src.da);
    }

    list_for_each_entry_iter(pass, iter, &pl->passes, entry) {
        mq_release(&pass->mq);
        ref_put(pass->fbo);
        free(pass);
    }
}
DECLARE_REFCLASS(pipeline);

struct pipeline *pipeline_new(struct scene *s)
{
    struct pipeline *pl;

    CHECK(pl = ref_new(pipeline));
    list_init(&pl->passes);
    pl->scene = s;

    return pl;
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
    CHECK(pass->fbo = fbo_new_ms(pl->scene->width, pl->scene->height, ms, nr_targets));

    if (src) {
        struct render_pass **psrc = darray_add(&pass->src.da);
        if (!psrc)
            goto err_fbo;

        *psrc = src;
    }

    pass->blit = ms;
    pass->blit_src = target;
    mq_init(&pass->mq, NULL);

    if (!prog_name)
        return pass;

    p = shader_prog_find(&pl->scene->shaders, prog_name);
    if (!p)
        goto err_src;

    m = model3d_new_quad(p, -1, 1, 0.1, 2, -2);
    m->cull_face = false;
    m->alpha_blend = false;
    txm = model3dtx_new_texture(ref_pass(m), &pass->fbo->tex);
    mq_add_model(&pass->mq, txm);
    e = entity3d_new(txm);
    list_append(&txm->entities, &e->entry);
    e->visible = true;
    mat4x4_identity(e->mx->m);
    ref_put(p);

    return pass;

err_src:
    darray_clearout(&pass->src.da);
err_fbo:
    ref_put_last(pass->fbo);
    free(pass);
    return NULL;
}

void pipeline_pass_repeat(struct render_pass *pass, struct render_pass *repeat, int count)
{
    pass->repeat = repeat;
    pass->rep_total = count;
}

void pipeline_pass_add_source(struct render_pass *pass, int to, struct render_pass *src)
{
    struct model3dtx *txmsrc = list_first_entry(&src->mq.txmodels, struct model3dtx, entry);
    struct model3dtx *txm = list_first_entry(&pass->mq.txmodels, struct model3dtx, entry);
    struct render_pass **psrc = darray_add(&pass->src.da);

    if (!psrc)
        return;

    *psrc = src;
    model3dtx_set_texture(txm, to, &src->fbo->tex);
}

void pipeline_render(struct pipeline *pl)
{
    struct scene *s = pl->scene;
    struct render_pass *first_pass = list_first_entry(&pl->passes, struct render_pass, entry);
    struct render_pass *last_pass = list_last_entry(&pl->passes, struct render_pass, entry);
    struct render_pass *pass = first_pass;
    struct render_pass *ppass = NULL;
    bool repeating = false;

    list_for_each_entry(pass, &pl->passes, entry) {
        if (!ppass || !repeating || ppass->rep_count)
            ppass = darray_count(pass->src) ? pass->src.x[0] : NULL;

        /*
         * This renders the contents of @ppass, using its shader into
         * @pass texture.
         */
repeat:
        if (ppass && ppass->blit) {
            fbo_prepare(pass->fbo);
            GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pass->fbo->fbo));
            GL(glBindFramebuffer(GL_READ_FRAMEBUFFER, ppass->fbo->fbo));
            GL(glReadBuffer(GL_COLOR_ATTACHMENT0 + pass->blit_src));
            GL(glBlitFramebuffer(0, 0, ppass->fbo->width, ppass->fbo->height,
                                 0, 0, pass->fbo->width, pass->fbo->height,
                                 GL_COLOR_BUFFER_BIT, GL_LINEAR));
            fbo_done(pass->fbo, s->width, s->height);
        } else {
            fbo_prepare(pass->fbo);
            GL(glDisable(GL_DEPTH_TEST));
            GL(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
            GL(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));
            if (pass == first_pass)
                models_render(&s->mq, &s->light, &s->cameras[0], s->proj_mx, s->focus, s->width, s->height, NULL);
            else
                models_render(&ppass->mq, NULL, NULL, NULL, NULL, s->width, s->height, NULL);

            fbo_done(pass->fbo, s->width, s->height);
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

