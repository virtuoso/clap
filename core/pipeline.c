// SPDX-License-Identifier: Apache-2.0
#include "model.h"
#include "pipeline.h"
#include "scene.h"
#include "shader.h"

struct render_pass {
    struct fbo          *fbo;
    struct mq           mq;
    struct list         entry;
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

void pipeline_add_pass(struct pipeline *pl, const char *prog_name)
{
    struct render_pass *pass;
    struct shader_prog *p;
    struct model3dtx *txm;
    struct entity3d *e;
    struct model3d *m;

    CHECK(pass = calloc(1, sizeof(*pass)));
    list_append(&pl->passes, &pass->entry);
    CHECK(pass->fbo = fbo_new(pl->scene->width, pl->scene->height));

    mq_init(&pass->mq, NULL);
    p = shader_prog_find(pl->scene->prog, prog_name);
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
}

void pipeline_render(struct pipeline *pl)
{
    struct scene *s = pl->scene;
    struct render_pass *pass = list_first_entry(&pl->passes, struct render_pass, entry);
    struct render_pass *last_pass = list_last_entry(&pl->passes, struct render_pass, entry);
    struct render_pass *ppass = pass;

    /* pass 0: render scene models */
    if (pass == last_pass)
        goto render;

    fbo_prepare(pass->fbo);
    GL(glEnable(GL_DEPTH_TEST));
    GL(glClearColor(0.2f, 0.2f, 0.6f, 1.0f));
    GL(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));
    models_render(&s->mq, &s->light, &s->cameras[0], s->proj_mx, s->focus, s->width, s->height, NULL);
    fbo_done(pass->fbo, s->width, s->height);
    goto next;

    list_for_each_entry(pass, &pl->passes, entry) {
render:
        if (pass != last_pass) {
            fbo_prepare(pass->fbo);
            GL(glDisable(GL_DEPTH_TEST));
            GL(glClearColor(0.2f, 0.2f, 0.6f, 1.0f));
        } else {
            GL(glEnable(GL_DEPTH_TEST));
        }
        GL(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));
        models_render(&ppass->mq, NULL, NULL, NULL, NULL, s->width, s->height, NULL);
        if (pass != last_pass)
            fbo_done(pass->fbo, s->width, s->height);
next:
        ppass = pass;
    }
}

