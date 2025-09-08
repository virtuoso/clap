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
    if (!opts->name || !opts->clap_ctx || !opts->width || !opts->height)
        return CERR_INVALID_ARGUMENTS;

    struct pipeline *pl = container_of(ref, struct pipeline, ref);
    list_init(&pl->passes);
    list_init(&pl->shaders);
    pl->render_options  = clap_get_render_options(opts->clap_ctx);
    pl->renderer        = clap_get_renderer(opts->clap_ctx);
    pl->shader_ctx      = clap_get_shaders(opts->clap_ctx);
    pl->camera          = opts->camera;
    pl->light           = opts->light;
    pl->name            = opts->name;
    pl->width           = opts->width;
    pl->height          = opts->height;
    pl->ssao_state      = opts->ssao_state;
    pl->nr_cascades     = opts->nr_cascades;

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
                    texture_t *tex = pass->use_tex[i] ?
                        pass->use_tex[i] :
                        fbo_texture(source->pass->fbo, source->attachment);

                    for (enum shader_vars v = ATTR_MAX; v < UNIFORM_TEX_MAX; v++) {
                        auto txm_tex = CRES_RET(model3dtx_texture(txm, v), continue);
                        if (txm_tex == tex) model3dtx_set_texture(txm, v, NULL);
                    }
                }
                /*
                 * model3dtx_drop() will unload model3dtx::lut, which should
                 * not happen, as LUTs are maintained globally. Prevent this
                 * from happening.
                 */
                model3dtx_set_texture(txm, UNIFORM_LUT_TEX, NULL);
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

    shaders_free(&pl->shaders);
}
DEFINE_REFCLASS2(pipeline);

cresp(shader_prog) pipeline_shader_find_get(pipeline *pl, const char *name)
{
    return shader_prog_find_get(pl->shader_ctx, &pl->shaders, name);
}

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

cresp(render_pass) _pipeline_add_pass(struct pipeline *pl, const pipeline_pass_config *cfg)
{
    if (!cfg->source || !cfg->ops || !cfg->ops->resize || !cfg->ops->prepare)
        return cresp_error(render_pass, CERR_INVALID_ARGUMENTS);

    /* Either shader or shader_override must be present, but not together */
    if (cfg->shader && cfg->shader_override)
        return cresp_error(render_pass, CERR_INVALID_ARGUMENTS);

    render_pass *pass = mem_alloc(sizeof(*pass), .zero = 1);
    if (!pass)
        return cresp_error(render_pass, CERR_NOMEM);

    list_append(&pl->passes, &pass->entry);

    for (pass->nr_sources = 0;
         cfg->source[pass->nr_sources].pass || cfg->source[pass->nr_sources].mq;
         pass->nr_sources++)
        ;


    /* Must have at least one source */
    cerr err = CERR_INVALID_ARGUMENTS;
    if (!pass->nr_sources)
        goto err_free;

    err = CERR_NOMEM;
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

    pass->fbo = CRES_RET(
        fbo_new(
            .width               = width,
            .height              = height,
            .layers              = cfg->layers,
            .color_format        = cfg->color_format,
            .depth_format        = cfg->depth_format,
            .multisampled        = cfg->multisampled,
            .attachment_config   = cfg->attachment_config
        ),
        { err = cerr_error_cres(__resp); goto err_source; }
    );

    err = CERR_NOMEM;
    pass->blit_fbo = mem_alloc(sizeof(fbo_t *), .nr = pass->nr_sources);
    if (!pass->blit_fbo)
        goto err_fbo;

    pass->use_tex = mem_alloc(sizeof(texture_t *), .nr = pass->nr_sources);
    if (!pass->use_tex)
        goto err_blit_fbo;

    err = CERR_INVALID_ARGUMENTS;
    int nr_blits = 0, nr_renders = 0, nr_uses = 0, nr_plugs = 0;
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
                pass->blit_fbo[i] = CRES_RET(
                    fbo_new(
                        .width                = fbo_width(src_pass->fbo),
                        .height               = fbo_height(src_pass->fbo),
                        .attachment_config    = { .depth_texture = 1 },
                        .multisampled         = fbo_is_multisampled(pass->fbo),
                        .depth_format         = fbo_texture_format(src_pass->fbo, rsrc->attachment)
                    ),
                    { err = cerr_error_cres(__resp); goto err_use_tex; }
                );
            } else if (rsrc->attachment.color_buffers || rsrc->attachment.color_textures) {
                if (!fbo_attachment_valid(src_pass->fbo, rsrc->attachment))
                    goto err_use_tex;

                pass->blit_fbo[i] = CRES_RET(
                    fbo_new(
                        .width                = fbo_width(src_pass->fbo),
                        .height               = fbo_height(src_pass->fbo),
                        .multisampled         = fbo_is_multisampled(pass->fbo),
                        .attachment_config    = { .color_texture0 = 1 },
                        .color_format         = (texture_format[]) {
                            fbo_texture_format(src_pass->fbo, rsrc->attachment)
                        }
                    ),
                    { err = cerr_error_cres(__resp); goto err_use_tex; }
                );
            } else {
                goto err_use_tex;
            }

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
        } else if (rsrc->method == RM_PLUG) {
            if (!rsrc->tex)
                goto err_use_tex;

            pass->use_tex[i] = rsrc->tex;
            nr_plugs++;
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

        pass->prog_override = CRES_RET(
            shader_prog_find_get(pl->shader_ctx, &pl->shaders, cfg->shader_override),
            { err = cerr_error_cres(__resp); goto err_use_tex; }
        );
    } else if (cfg->shader) {
        if (!nr_blits && !nr_uses && !nr_plugs)
            goto err_use_tex;

        prog = CRES_RET(
            shader_prog_find_get(pl->shader_ctx, &pl->shaders, cfg->shader),
            { err = cerr_error_cres(__resp); goto err_override; }
        );

        err = CERR_NOMEM;
        m = model3d_new_quad(prog, -1, 1, 0, 2, -2);
        if (!m)
            goto err_prog;

        m->depth_testing = false;
        m->alpha_blend = false;

        txm = CRES_RET(
            ref_new_checked(model3dtx, .model = ref_pass(m)),
            { err = cerr_error_cres(__resp); goto err_model; }
        );

        for (int i = 0; i < pass->nr_sources; i++) {
            render_source *rsrc = &pass->source[i];

            if (rsrc->method == RM_RENDER)
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
        pass->quad = CRES_RET(
            ref_new_checked(entity3d, .txmodel = ref_pass(txm)),
            { err = cerr_error_cres(__resp); goto err_txmodel; }
        );

        pass->quad->skip_culling = true;
        mat4x4_identity(pass->quad->mx);
        ref_put(prog);
    }

    pipeline_dropdown_push(pl, pass);

    return cresp_val(render_pass, pass);

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
    return cresp_error_cerr(render_pass, err);
}

cresp(render_pass) pipeline_find_pass(pipeline *pl, const char *name)
{
    render_pass *pass;

    list_for_each_entry(pass, &pl->passes, entry)
        if (!strcmp(pass->name, name))
            return cresp_val(render_pass, pass);

    return cresp_error_cerr(render_pass, CERR_NOT_FOUND);
}

void render_pass_plug_texture(render_pass *pass, enum shader_vars sampler, texture_t *tex)
{
    for (int i = 0; i < pass->nr_sources; i++) {
        if (pass->source[i].sampler == sampler && pass->source[i].method == RM_PLUG) {
            pass->source[i].tex = tex;
            model3dtx_set_texture(pass->quad->txmodel, sampler, tex);
            return;
        }
    }
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
    unsigned long count = 0, culled = 0;
    RENDER_PASS_OPS_PARAMS(pl, pass);

    pass->ops->prepare(&params);

    if (mq) {
        /* Render render_source::mq models */
        models_render(pl->renderer, mq,
                      .shader_override  = pass->prog_override,
                      .render_options   = pl->render_options,
                      .light            = params.light,
                      .camera           = params.camera,
                      .width            = fbo_width(pass->fbo),
                      .height           = fbo_height(pass->fbo),
                      .cascade          = pass->cascade,
                      .nr_cascades      = pl->nr_cascades,
                      .entity_count     = &count,
                      .culled_count     = &culled);
    } else {
        /* Render our postprocessing quad */
        struct mq _mq; /* XXX: -> pass->mq, then mq_release() on drop */

        mq_init(&_mq, NULL);
        mq_add_model(&_mq, pass->quad->txmodel);
        models_render(pl->renderer, &_mq,
                      .camera           = params.camera,
                      .near_plane       = params.near_plane,
                      .far_plane        = params.far_plane,
                      .render_options   = pl->render_options,
                      .width            = fbo_width(pass->fbo),
                      .height           = fbo_height(pass->fbo),
                      .ssao_state       = pl->ssao_state,
                      .cascade          = -1,
                      .nr_cascades      = pl->nr_cascades,
                      .entity_count     = &count);
        list_del(&pass->quad->txmodel->entry);
    }

    pipeline_pass_debug_end(pl, count, culled);
}

void pipeline_render(struct pipeline *pl, unsigned int checkpoint)
{
    if (!pl || list_empty(&pl->passes))
        return;

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
