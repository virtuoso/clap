// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "memory.h"
#include "model.h"
#include "pipeline.h"
#include "pipeline-debug.h"
#include "pipeline-internal.h"
#include "primitives.h"
#include "shader.h"
#include "ui-debug.h"

texture_t *pipeline_pass_get_texture(struct render_pass *pass, fbo_attachment attachment)
{
    return fbo_texture(pass->fbo, attachment);
}

float pipeline_pass_get_scale(render_pass *pass)
{
    return pass->scale;
}

static cerr pipeline_make(struct ref *ref, void *_opts)
{
    rc_init_opts(pipeline) *opts = _opts;
    if (!opts->name || !opts->renderer || !opts->shaders || !opts->width || !opts->height)
        return CERR_INVALID_ARGUMENTS;

    struct pipeline *pl = container_of(ref, struct pipeline, ref);
    list_init(&pl->passes);
    pl->render_options  = opts->render_options;
    pl->renderer        = opts->renderer;
    pl->shaders         = opts->shaders;
    pl->camera          = opts->camera;
    pl->light           = opts->light;
    pl->name            = opts->name;
    pl->width           = opts->width;
    pl->height          = opts->height;

    pipeline_debug_init(pl);

    return CERR_OK;
}

void pipeline_clearout(pipeline *pl)
{
    struct render_pass *pass, *iter;

    pipeline_debug_done(pl);

    /*
     * We need 2 passes, because @pass sources reference elements behind itself
     * in the list, and in order to undo this relationship, we need the source
     * element to not be freed
     */
    list_for_each_entry(pass, &pl->passes, entry) {
        /*
         * Depth/color attachments don't have quad models,
         * so the below would cause a massive memory violation
         * that for some reason no ASAN can catch, so naturally
         * skip it if the mq list is empty
         */
        if (pass->quad) {
            model3dtx *txm = pass->quad->txmodel;

            for (int i = 0; i < pass->nr_sources; i++) {
                render_source *source = &pass->source[i];

                if (source->pass) {
                    fbo_t *fbo = source->pass->fbo;

                    if (txm->texture == fbo_texture(fbo, FBO_COLOR_TEXTURE(0)))
                        txm->texture = &txm->_texture;
                    if (txm->emission == fbo_texture(fbo, FBO_COLOR_TEXTURE(0)))
                        txm->emission = &txm->_emission;
                    if (txm->sobel == fbo_texture(fbo, FBO_COLOR_TEXTURE(0)))
                        txm->sobel = &txm->_sobel;
                }
            }

            ref_put_last(pass->quad);
        }
    }

    list_for_each_entry_iter(pass, iter, &pl->passes, entry) {
        for (int i = 0; i < pass->nr_sources; i++)
            if (pass->blit_fbo[i])
                fbo_put(pass->blit_fbo[i]);

        fbo_put(pass->fbo);
        if (pass->prog_override)
            ref_put(pass->prog_override);

        mem_free(pass->blit_fbo);
        mem_free(pass->use_tex);
        mem_free(pass->source);
        list_del(&pass->entry);
        mem_free(pass);
    }
}

static void pipeline_drop(struct ref *ref)
{
    struct pipeline *pl = container_of(ref, struct pipeline, ref);

    pipeline_clearout(pl);
}
DEFINE_REFCLASS2(pipeline);

void pipeline_resize(struct pipeline *pl, unsigned int width, unsigned int height)
{
    struct render_pass *pass;

    list_for_each_entry(pass, &pl->passes, entry) {
        RENDER_PASS_OPS_PARAMS(pl, pass);
        cerr err;

        /* First, resize blit_fbo[]s to match the fbos they're blitting from */
        for (int i = 0; i < pass->nr_sources; i++) {
            render_pass *src_pass = pass->source[i].pass;
            if (!src_pass)
                continue;

            unsigned int _width = width, _height = height;
            if (pass->blit_fbo[i]) {
                /* use src_pass' resize() to obtain dimensions */
                params.render_scale = src_pass->scale;
                src_pass->ops->resize(&params, &_width, &_height);
                err = fbo_resize(pass->blit_fbo[i], _width, _height);
                if (IS_CERR(err))
                    err_cerr(err, "pass '%s': error resizing blit FBO to %d x %d\n",
                             pass->name, _width, _height);
            }
        }

        /* Then, resize our fbo */
        unsigned int _width = width, _height = height;
        params.render_scale = pass->scale;
        pass->ops->resize(&params, &_width, &_height);
        err = fbo_resize(pass->fbo, _width, _height);
        if (IS_CERR(err))
            err_cerr(err, "pass '%s': error resizing FBO to %d x %d\n",
                     pass->name, _width, _height);
    }

    pl->width = width;
    pl->height = height;
}

struct render_pass *_pipeline_add_pass(struct pipeline *pl, const pipeline_pass_config *cfg)
{
    if (!cfg->source || !cfg->ops || !cfg->ops->resize || !cfg->ops->prepare)
        return NULL;

    /* Either shader or shader_override must be present, but not together */
    if (cfg->shader && cfg->shader_override)
        return NULL;

    render_pass *pass = mem_alloc(sizeof(*pass), .zero = 1);
    if (!pass)
        return NULL;

    list_append(&pl->passes, &pass->entry);

    for (pass->nr_sources = 0;
         cfg->source[pass->nr_sources].pass || cfg->source[pass->nr_sources].mq;
         pass->nr_sources++)
        ;

    /* Must have at least one source */
    if (!pass->nr_sources)
        goto err_free;

    pass->source = memdup(cfg->source, sizeof(render_source) * pass->nr_sources);
    if (!pass->source)
        goto err_free;

    pass->ops = cfg->ops;
    pass->name = cfg->name;
    pass->checkpoint = cfg->checkpoint;

    if (!pass->name)
        pass->name = cfg->shader;

    if (cfg->shader_override && !pass->name)
        pass->name = cfg->shader_override;

    pass->cascade = cfg->cascade;
    pass->scale = cfg->scale ? : 1.0;
    unsigned int width = pl->width, height = pl->height;
    RENDER_PASS_OPS_PARAMS(pl, pass);
    pass->ops->resize(&params, &width, &height);

    cresp(fbo_t) res = fbo_new(.width               = width,
                               .height              = height,
                               .layers              = cfg->layers,
                               .color_format        = cfg->color_format,
                               .depth_format        = cfg->depth_format,
                               .multisampled        = cfg->multisampled,
                               .attachment_config   = cfg->attachment_config);
    if (IS_CERR(res))
        goto err_source;

    pass->fbo = res.val;

    pass->blit_fbo = mem_alloc(sizeof(fbo_t *), .nr = pass->nr_sources);
    if (!pass->blit_fbo)
        goto err_fbo;

    pass->use_tex = mem_alloc(sizeof(texture_t *), .nr = pass->nr_sources);
    if (!pass->blit_fbo)
        goto err_blit_fbo;

    int nr_blits = 0, nr_renders = 0, nr_uses = 0;
    for (int i = 0; i < pass->nr_sources; i++) {
        render_source *rsrc = &pass->source[i];

        if (rsrc->method == RM_BLIT) {
            /*
             * Set up blit_fbo[i] as a color texture buffer for blitting from rsrc->pass->fbo
             * attachment rsrc->attachment with the color format of that attachment
             */
            render_pass *src_pass = rsrc->pass;
            if (!src_pass || !src_pass->fbo)
                goto err_use_tex;

            if (rsrc->attachment.depth_buffer || rsrc->attachment.depth_texture) {
                res = fbo_new(.width                = fbo_width(src_pass->fbo),
                              .height               = fbo_height(src_pass->fbo),
                              .attachment_config    = { .depth_texture = 1 },
                              .multisampled         = fbo_is_multisampled(pass->fbo),
                              .depth_format         = fbo_texture_format(src_pass->fbo, rsrc->attachment));
            } else if (rsrc->attachment.color_buffers || rsrc->attachment.color_textures) {
                if (!fbo_attachment_valid(src_pass->fbo, rsrc->attachment))
                    goto err_use_tex;

                res = fbo_new(.width                = fbo_width(src_pass->fbo),
                              .height               = fbo_height(src_pass->fbo),
                              .multisampled         = fbo_is_multisampled(pass->fbo),
                              .attachment_config    = { .color_texture0 = 1 },
                              .color_format         = (texture_format[]) {
                                    fbo_texture_format(src_pass->fbo, rsrc->attachment)
                              });
            } else {
                goto err_use_tex;
            }

            if (IS_CERR(res))
                goto err_use_tex;

            pass->blit_fbo[i] = res.val;

            nr_blits++;
        } else if (rsrc->method == RM_RENDER) {
            if (!rsrc->mq)
                goto err_use_tex;

            nr_renders++;
        } else if (rsrc->method == RM_USE) {
            if (!rsrc->pass)
                goto err_use_tex;

            pass->use_tex[i] = fbo_texture(rsrc->pass->fbo, rsrc->attachment);
            if (!pass->use_tex[i])
                goto err_use_tex;

            nr_uses++;
        } else {
            goto err_use_tex;
        }
    }

    struct shader_prog *prog;
    model3dtx *txm;
    model3d *m;

    if (cfg->shader_override) {
        if (!nr_renders)
            goto err_use_tex;

        pass->prog_override = shader_prog_find(pl->shaders, cfg->shader_override);
        if (!pass->prog_override)
            goto err_use_tex;
    } else if (cfg->shader) {
        if (!nr_blits && !nr_uses)
            goto err_use_tex;

        prog = shader_prog_find(pl->shaders, cfg->shader);
        if (!prog)
            goto err_override;

        m = model3d_new_quad(prog, -1, 1, 0, 2, -2);
        if (!m)
            goto err_prog;

        m->depth_testing = false;
        m->alpha_blend = false;

        txm = ref_new(model3dtx, .model = ref_pass(m));//, .tex = fbo_texture(pass->fbo));
        if (!txm)
            goto err_model;

        for (int i = 0; i < pass->nr_sources; i++) {
            render_source *rsrc = &pass->source[i];

            if (rsrc->method != RM_BLIT && rsrc->method != RM_USE)
                continue;

            if (pass->blit_fbo[i])
                model3dtx_set_texture(txm, rsrc->sampler, fbo_texture(pass->blit_fbo[i], FBO_COLOR_TEXTURE(0)));
            else
                model3dtx_set_texture(txm, rsrc->sampler, pass->use_tex[i]);
        }

        /*
         * ref_pass() because this txmodel is only ever on a temporary mq and doesn't
         * ever get released via mq_release() (like regular txmodels), on the other
         * hand, it's a 1:1 between this quad and this txmodel, so make the quad hold
         * the only reference to the txmodel so it gets freed on ref_put(pass->quad).
         */
        pass->quad = ref_new(entity3d, .txmodel = ref_pass(txm));
        if (!pass->quad)
            goto err_txmodel;

        mat4x4_identity(pass->quad->mx);
        ref_put(prog);
    }

    pipeline_dropdown_push(pl, pass);

    return pass;

err_txmodel:
    ref_put_last(txm);
err_model:
    ref_put_last(m);
err_prog:
    ref_put(prog);
err_override:
    if (pass->prog_override)
        ref_put(pass->prog_override);
err_use_tex:
    mem_free(pass->use_tex);
err_blit_fbo:
    for (int i = 0; i < pass->nr_sources; i++)
        if (pass->blit_fbo[i])
            fbo_put(pass->blit_fbo[i]);
    mem_free(pass->blit_fbo);
err_fbo:
    fbo_put_last(pass->fbo);
err_source:
    mem_free(pass->source);
err_free:
    list_del(&pass->entry);
    mem_free(pass);
    return NULL;
}

/*
 * Copy in stuff that needs copying from source passes, return mq if
 * one of the sources has it.
 */
static struct mq *pass_resolve_sources(pipeline *pl, render_pass *pass)
{
    struct mq *mq = NULL;

    /* Blit stuff to blit_fbo[]s, pick up mq is one was given */
    for (int i = 0; i < pass->nr_sources; i++) {
        pipeline_pass_debug_begin(pl, pass, i);

        render_source *rsrc = &pass->source[i];

        if (rsrc->method == RM_RENDER) {
            if (mq) {
                err("pass '%s' has multiple RM_RENDER sources\n", pass->name);
                continue;
            }
            mq = rsrc->mq;
            continue;
        }

        if (rsrc->method != RM_BLIT)
            continue;

        fbo_t *fbo = pass->blit_fbo[i];
        if (!fbo) {
            err("pass '%s' source %d blitting into a NULL FBO\n", pass->name, i);
            continue;
        }

        fbo_prepare(fbo);
        fbo_blit_from_fbo(fbo, rsrc->pass->fbo, rsrc->attachment);
        fbo_done(fbo, pl->width, pl->height);
    }

    return mq;
}

/*
 * Render one pass either to its framebuffer (the caller does fbo_prepare()/fbo_done())
 * or to the screen.
 */
static void pass_render(pipeline *pl, render_pass *pass, struct mq *mq)
{
    unsigned long count = 0;
    RENDER_PASS_OPS_PARAMS(pl, pass);

    pass->ops->prepare(&params);

    if (mq) {
        /* Render render_source::mq models */
        models_render(pl->renderer, mq,
                      .shader_override  = pass->prog_override,
                      .render_options   = pl->render_options,
                      .light            = params.light,
                      .camera           = params.camera,
                      .cascade          = pass->cascade,
                      .entity_count     = &count);
    } else {
        /* Render our postprocessing quad */
        struct mq _mq; /* XXX: -> pass->mq, then mq_release() on drop */

        mq_init(&_mq, NULL);
        mq_add_model(&_mq, pass->quad->txmodel);
        models_render(pl->renderer, &_mq,
                      .near_plane       = params.near_plane,
                      .far_plane        = params.far_plane,
                      .render_options   = pl->render_options,
                      .entity_count     = &count);
        list_del(&pass->quad->txmodel->entry);
    }

    pipeline_pass_debug_end(pl, count);
}

void pipeline_render(struct pipeline *pl, unsigned int checkpoint)
{
    struct render_pass *last_pass = list_last_entry(&pl->passes, struct render_pass, entry);
    struct render_pass *pass;
    struct mq *mq = NULL;
    bool stop = false;

    pipeline_debug_begin(pl);

    list_for_each_entry(pass, &pl->passes, entry) {
        /* Prepare to render from pass' sources */
        mq = pass_resolve_sources(pl, pass);

        /*
         * "checkpoint" is a mark of a render pass at which the caller can request
         * to stop rendering; this render pass will be rendered on the screen instead
         * of its framebuffer. Useful for having a few extra blur stages at the end
         * for when the modal UI needs to come in.
         */
        if (pass->checkpoint > checkpoint) {
            stop = true;
            break;
        }

        fbo_prepare(pass->fbo);
        pass_render(pl, pass, mq);
        fbo_done(pass->fbo, pl->width, pl->height);
    }

    if (!stop)
        pass = last_pass;

    /* render the last pass to the screen */
    pass_render(pl, pass, mq);

    pipeline_debug_end(pl);
}
