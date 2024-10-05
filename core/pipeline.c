// SPDX-License-Identifier: Apache-2.0
#include "model.h"
#include "pipeline.h"
#include "scene.h"
#include "shader.h"

struct render_pass {
    struct render_pass  *src;
    struct fbo          *fbo;
    struct mq           mq;
    struct list         entry;
    bool                blit;
};

static void pipeline_drop(struct ref *ref)
{
    struct pipeline *pl = container_of(ref, struct pipeline, ref);
    struct render_pass *pass, *iter;

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

struct render_pass *pipeline_add_pass(struct pipeline *pl, struct render_pass *src, const char *prog_name, bool ms)
{
    struct render_pass *pass;
    struct shader_prog *p;
    struct model3dtx *txm;
    struct entity3d *e;
    struct model3d *m;

    CHECK(pass = calloc(1, sizeof(*pass)));
    list_append(&pl->passes, &pass->entry);
    CHECK(pass->fbo = fbo_new_ms(pl->scene->width, pl->scene->height, ms));

    pass->src = src;
    pass->blit = ms;
    mq_init(&pass->mq, NULL);

    if (!prog_name)
        return pass;

    p = shader_prog_find(&pl->scene->shaders, prog_name);
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
}

void pipeline_render(struct pipeline *pl)
{
    struct scene *s = pl->scene;
    struct render_pass *first_pass = list_first_entry(&pl->passes, struct render_pass, entry);
    struct render_pass *last_pass = list_last_entry(&pl->passes, struct render_pass, entry);
    struct render_pass *pass = first_pass;
    struct render_pass *ppass = NULL;

    list_for_each_entry(pass, &pl->passes, entry) {
        ppass = pass->src;
        /*
         * This renders the contents of @ppass, using its shader into
         * @pass texture.
         */
        if (ppass && ppass->blit) {
            fbo_prepare(pass->fbo);
            GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pass->fbo->fbo));
            GL(glBindFramebuffer(GL_READ_FRAMEBUFFER, ppass->fbo->fbo));
            GL(glBlitFramebuffer(0, 0, ppass->fbo->width, ppass->fbo->height,
                                 0, 0, pass->fbo->width, pass->fbo->height,
                                 GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST));
             fbo_done(pass->fbo, s->width, s->height);
        } else {
            fbo_prepare(pass->fbo);
            GL(glDisable(GL_DEPTH_TEST));
            GL(glClearColor(0.2f, 0.2f, 0.6f, 1.0f));
            GL(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));
            if (pass == first_pass)
                models_render(&s->mq, &s->light, &s->cameras[0], s->proj_mx, s->focus, s->width, s->height, NULL);
            else
                models_render(&ppass->mq, NULL, NULL, NULL, NULL, s->width, s->height, NULL);

            fbo_done(pass->fbo, s->width, s->height);
        }
    }

    /* render the last pass to the screen */
    GL(glEnable(GL_DEPTH_TEST));
    GL(glClearColor(0.2f, 0.2f, 0.6f, 1.0f));
    GL(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));
    models_render(&last_pass->mq, NULL, NULL, NULL, NULL, s->width, s->height, NULL);
}

