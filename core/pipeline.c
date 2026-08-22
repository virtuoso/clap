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
#include "util.h"

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
    if (!opts->name || !opts->clap_ctx)
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
    pl->ssao_state      = opts->ssao_state;
    pl->nr_cascades     = opts->nr_cascades;

    clap_get_viewport(opts->clap_ctx, NULL, NULL, &pl->width, &pl->height);

    pipeline_debug_init(pl);

    return CERR_OK;
}

static void pass_free_quad(render_pass *pass)
{
    auto txm = pass->quad->txmodel;

    // Detach all external textures (RM_USE/RM_PLUG) either from other
    // passes, where they will be released by their ref_put(pass->quad),
    // or from other external sources that will manage their textures'
    // lifetimes themselves, like color grading LUTs.
    for (int i = 0; i < pass->nr_sources; i++) {
        render_source *source = &pass->source[i];

        switch (source->method) {
            case RM_USE:
            case RM_PLUG:
            case RM_HISTORY:
                model3dtx_set_texture(txm, source->sampler, NULL);
                break;
            default:        continue;
        }
    }

    /* models_render() plugs these into use_light passes' quads (model.c) */
    if (pass->use_light) {
        model3dtx_set_texture(txm, UNIFORM_LIGHT_MAP, NULL);
        model3dtx_set_texture(txm, UNIFORM_SHADOW_MAP, NULL);
        model3dtx_set_texture(txm, UNIFORM_SHADOW_MAP1, NULL);
        model3dtx_set_texture(txm, UNIFORM_SHADOW_MAP2, NULL);
        model3dtx_set_texture(txm, UNIFORM_SHADOW_MAP3, NULL);
        model3dtx_set_texture(txm, UNIFORM_SHADOW_MAP_MS, NULL);
    }

    ref_put_last(pass->quad);
}

void pipeline_clearout(pipeline *pl)
{
    struct render_pass *pass, *iter;

    pipeline_debug_done(pl);

    list_for_each_entry_iter(pass, iter, &pl->passes, entry) {
        if (pass->quad) pass_free_quad(pass);

        for (int i = 0; i < pass->nr_sources; i++)
            if (pass->blit_fbo[i])
                fbo_put(pass->blit_fbo[i]);

        fbo_put(pass->fbo);
        if (pass->fbo_back) fbo_put(pass->fbo_back);
        if (pass->prog_override)
            ref_put(pass->prog_override);

        mem_free(pass->blit_fbo);
        mem_free(pass->use_tex);
        mem_free(pass->source);
        list_del(&pass->entry);
        mem_free(pass);
    }

    /* Pass teardown took the history buffers with it */
    pl->history_valid = false;
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

render_options *pipeline_render_options(pipeline *pl)
{
    return pl->render_options;
}

bool pipeline_shaders_are_stale(pipeline *pl)
{
    return pl->shaders_stale;
}

cresp(shader_prog) pipeline_shader_variant_get(pipeline *pl, const char *base,
                                               shader_variants_t variants)
{
    auto res = shader_get_name(base, variants);

    /* no exact specialization compiled: the uber @base covers any mask */
    if (IS_CERR_CODE(res, CERR_NOT_FOUND))  return pipeline_shader_find_get(pl, base);

    return pipeline_shader_find_get(pl, CRES_RET_T(res, shader_prog));
}

cerr pipeline_model_shadow_resolve(pipeline *pl, model3d *model)
{
    const render_options *ropts = pl->render_options;

    if (!model->shader_base)    return CERR_OK;

    /*
     * The family follows the shadow passes' own choice
     * (pipeline-builder.c); a rebuild is what changes it, and that's
     * where this runs from. The mask is the model's attribute-layout
     * bits -- which is the whole point: the shadow prog reads the same
     * vertex buffer as @prog.
     */
    bool vsm = ropts->shadow_vsm;
    const char *base = vsm ? "shadow_vsm" : "shadow";
    shader_variants_t variants =
        model->variants & (SHADER_NORMALS_BIT | SHADER_SKINNING_BIT);

    struct shader_prog *prog = CRES_RET_CERR(
        pipeline_shader_variant_get(pl, base, variants));

    if (prog == model->shadow_prog) {
        ref_put(prog);
        return CERR_OK;
    }

    if (model->shadow_prog) ref_put(model->shadow_prog);
    model->shadow_prog = prog;

    return CERR_OK;
}

cerr pipeline_shaders_resolve(pipeline *pl, struct mq *mq)
{
    shader_variants_t global = ropts_shader_variants(pl->render_options);
    model3dtx *txm;

    pl->shaders_stale = false;

    list_for_each_entry(txm, &mq->txmodels, entry) {
        model3d *model = txm->model;

        if (!model->shader_base)    continue;

        /* @prog and @shadow_prog are functions of the mask alone */
        shader_variants_t variants = model3dtx_shader_variants(txm) | global;
        if (variants == model->variants)    continue;

        /*
         * Recorded first, and also when it maps onto the same prog (the
         * uber shader): a mask fails or resolves once, not on every
         * frame that draws the model
         */
        model->variants = variants;

        struct shader_prog *prog = CRES_RET_CERR(
            pipeline_shader_variant_get(pl, model->shader_base, variants));

        if (prog != model->prog) {
            dbg("model '%s': shader '%s' -> '%s'\n", model->name,
                shader_name(model->prog), shader_name(prog));
            ref_put(model->prog);
            model->prog = prog;
        } else {
            ref_put(prog);
        }

        /* the shadow prog tracks @prog's attribute set, always */
        CERR_RET_CERR(pipeline_model_shadow_resolve(pl, model));
    }

    return CERR_OK;
}

shader_variants_t ropts_shader_variants(const render_options *ropts)
{
    shader_variants_t variants = SHADER_SHADOWS_BIT;

    /* the CSM/PCF path only runs when vsm sampling is off */
    if (!ropts->shadow_vsm) variants |= SHADER_SHADOW_PCF_BIT;
    /* the velocity RT rides on the compile-time config, not ropts->taa */
    if (IS_DEFINED(CONFIG_TAA)) variants |= SHADER_TAA_BIT;

    return variants;
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

        if (pass->fbo_back)
            CERR_RET(fbo_resize(pass->fbo_back, _width, _height),
                     err_cerr(__cerr, "pass '%s': error resizing back FBO to %d x %d\n",
                              pass->name, _width, _height));
    }

    pl->width = width;
    pl->height = height;
    /* Freshly resized back buffers hold garbage */
    pl->history_valid = false;
}

cresp(render_pass) _pipeline_add_pass(struct pipeline *pl, const pipeline_pass_config *cfg)
{
    if (!cfg->source || !cfg->ops || !cfg->ops->resize || !cfg->ops->prepare)
        return cresp_error(render_pass, CERR_INVALID_ARGUMENTS);

    /* Either shader or shader_override must be present, but not together */
    if (cfg->shader && cfg->shader_override)
        return cresp_error(render_pass, CERR_INVALID_ARGUMENTS);

    /* Ping-pong render targets don't mix with MSAA */
    if (cfg->ping_pong && cfg->multisampled)    return cresp_error(render_pass, CERR_INVALID_ARGUMENTS);

    render_pass *pass = mem_alloc(sizeof(*pass), .zero = 1);
    if (!pass)
        return cresp_error(render_pass, CERR_NOMEM);

    list_append(&pl->passes, &pass->entry);

    for (pass->nr_sources = 0;
         cfg->source[pass->nr_sources].pass || cfg->source[pass->nr_sources].mq ||
         cfg->source[pass->nr_sources].method == RM_HISTORY;
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
    pass->use_light = cfg->use_light;
    pass->scale = cfg->scale ? : 1.0;
    unsigned int width = pl->width, height = pl->height;
    RENDER_PASS_OPS_PARAMS(pl, pass);
    pass->ops->resize(&params, &width, &height);

    pass->fbo = CRES_RET(
        fbo_new(
            .renderer            = pl->renderer,
            .name                = pass->name,
            .width               = width,
            .height              = height,
            .layers              = cfg->layers,
            .color_config        = cfg->color_config,
            .depth_config        = cfg->depth_config,
            .multisampled        = cfg->multisampled,
            .layout              = cfg->layout,
        ),
        { err = cerr_error_cres(__resp); goto err_source; }
    );

    if (cfg->ping_pong)
        pass->fbo_back = CRES_RET(
            fbo_new(
                .renderer            = pl->renderer,
                .name                = pass->name,
                .width               = width,
                .height              = height,
                .layers              = cfg->layers,
                .color_config        = cfg->color_config,
                .depth_config        = cfg->depth_config,
                .layout              = cfg->layout,
            ),
            { err = cerr_error_cres(__resp); goto err_fbo; }
        );

    err = CERR_NOMEM;
    pass->blit_fbo = mem_alloc(sizeof(fbo_t *), .nr = pass->nr_sources);
    if (!pass->blit_fbo)    goto err_fbo_back;

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
                        .renderer             = pl->renderer,
                        .width                = fbo_width(src_pass->fbo),
                        .height               = fbo_height(src_pass->fbo),
                        .layout               = FBO_DEPTH_TEXTURE(0),
                        .multisampled         = fbo_is_multisampled(pass->fbo),
                        .depth_config         = {
                            .format           = fbo_texture_format(src_pass->fbo, rsrc->attachment),
                            .load_action      = FBOLOAD_DONTCARE,
                            .depth_func       = DEPTH_FN_NEVER
                        }
                    ),
                    { err = cerr_error_cres(__resp); goto err_use_tex; }
                );
            } else if (rsrc->attachment.color_buffers || rsrc->attachment.color_textures) {
                if (!fbo_attachment_valid(src_pass->fbo, rsrc->attachment))
                    goto err_use_tex;

                pass->blit_fbo[i] = CRES_RET(
                    fbo_new(
                        .renderer             = pl->renderer,
                        .width                = fbo_width(src_pass->fbo),
                        .height               = fbo_height(src_pass->fbo),
                        .multisampled         = fbo_is_multisampled(pass->fbo),
                        .layout               = FBO_COLOR_TEXTURE(0),
                        .color_config         = (fbo_attconfig[]) {
                            {
                                .format       = fbo_texture_format(src_pass->fbo, rsrc->attachment),
                                .load_action  = FBOLOAD_DONTCARE
                            }
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

            texture_set_name(pass->use_tex[i], "%s:%s [rt]", pass->name, shader_get_var_name(rsrc->sampler));
            nr_uses++;
        } else if (rsrc->method == RM_PLUG) {
            if (!rsrc->tex)
                goto err_use_tex;

            pass->use_tex[i] = rsrc->tex;
            texture_set_name(pass->use_tex[i], "%s:%s", pass->name, shader_get_var_name(rsrc->sampler));
            nr_plugs++;
        } else if (rsrc->method == RM_HISTORY) {
            if (!pass->fbo_back)    goto err_use_tex;

            pass->use_tex[i] = fbo_texture(pass->fbo_back, rsrc->attachment);
            if (!pass->use_tex[i])  goto err_use_tex;

            texture_set_name(pass->use_tex[i], "%s:%s [history]", pass->name,
                             shader_get_var_name(rsrc->sampler));
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

        entity3d_set(pass->quad, ENTITY3D_SKIP_CULLING, NULL);
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
err_fbo_back:
    if (pass->fbo_back) fbo_put_last(pass->fbo_back);
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

    /* Ping-pong: last frame's render target becomes this frame's history */
    if (pass->fbo_back) {
        fbo_t *front = pass->fbo_back;

        pass->fbo_back = pass->fbo;
        pass->fbo = front;
    }

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

        /* Ping-pong textures change identity every frame; rebind */
        if (rsrc->method == RM_HISTORY) {
            pass->use_tex[i] = fbo_texture(pass->fbo_back, rsrc->attachment);
            if (pass->quad)     model3dtx_set_texture(pass->quad->txmodel, rsrc->sampler, pass->use_tex[i]);
            continue;
        }

        if (rsrc->method == RM_USE && rsrc->pass->fbo_back) {
            pass->use_tex[i] = fbo_texture(rsrc->pass->fbo, rsrc->attachment);
            if (pass->quad)     model3dtx_set_texture(pass->quad->txmodel, rsrc->sampler, pass->use_tex[i]);
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
static cerr pass_render(pipeline *pl, render_pass *pass, struct mq *mq)
{
    unsigned long count = 0, culled = 0;
    RENDER_PASS_OPS_PARAMS(pl, pass);

    pass->ops->prepare(&params);

    /* Jitter camera passes only; shadow_prepare() clears params.camera */
    taa_params *taa = params.camera && pl->taa_active ? &pl->taa : NULL;

    cerr ret = CERR_OK;

    if (mq) {
        /* Render render_source::mq models */
        ret = models_render(pl->renderer, mq,
                            .shader_override  = pass->prog_override,
                            .taa              = taa,
                            .variants_stale   = pass->prog_override ? NULL : &pl->shaders_stale,
                            .render_options   = pl->render_options,
                            .near_plane       = params.near_plane,
                            .far_plane        = params.far_plane,
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
        ret = models_render(pl->renderer, &_mq,
                            .camera           = params.camera,
                            .light            = pass->use_light ? params.light : NULL,
                            .taa              = taa,
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

    return ret;
}

/**
 * taa_update() - advance TAA sub-pixel jitter to the next frame
 * @pl:     pipeline to advance
 *
 * Each frame, the camera projection is offset by a sub-pixel amount so
 * that edges get rasterized at a different position within the pixel;
 * once the TAA resolve pass blends frames over time, those samples
 * average into gradients instead of stairsteps -- that is where the
 * extra spatial resolution comes from. The offsets step through a
 * Halton(2,3) sequence: the radical inverses of the frame counter in
 * bases 2 (x) and 3 (y), which spread 8 points evenly over the pixel
 * without the clumping of random points or the correlation of a grid.
 * Index 0 (the pixel corner) is skipped by convention.
 *
 * The jittered projection is derived from view::main per frame and
 * never written back, so frustum culling and shadow cascade fitting
 * only ever see the clean matrices.
 */
static void taa_update(pipeline *pl)
{
    pl->taa_active = pl->render_options->taa && pl->camera &&
                     pl->width && pl->height;
    if (!pl->taa_active)    return;

    /* history_valid still reflects the previous frame at this point */
    pl->taa.reset = !pl->history_valid;

    static const vec2 halton23[] = {
        { 0.50000f, 0.333333f }, { 0.25000f, 0.666667f },
        { 0.75000f, 0.111111f }, { 0.12500f, 0.444444f },
        { 0.62500f, 0.777778f }, { 0.37500f, 0.222222f },
        { 0.87500f, 0.555556f }, { 0.06250f, 0.888889f },
    };
    const float *h = halton23[pl->jitter_seq++ % array_size(halton23)];

    /* +/-0.5px in NDC units (one pixel spans 2/width) */
    pl->taa.jitter[0] = (h[0] - 0.5f) * 2.0f / (float)pl->width;
    pl->taa.jitter[1] = (h[1] - 0.5f) * 2.0f / (float)pl->height;

    mat4x4 jitter_mx;
    mat4x4_translate(jitter_mx, pl->taa.jitter[0], pl->taa.jitter[1], 0.0f);
    mat4x4_mul(pl->taa.proj_jittered, jitter_mx, pl->camera->view.main.proj_mx);
    mat4x4_invert(pl->taa.inv_proj_jittered, pl->taa.proj_jittered);
}

cerr pipeline_render(struct pipeline *pl, unsigned int checkpoint)
{
    if (!pl || list_empty(&pl->passes))
        return CERR_INVALID_ARGUMENTS_REASON(
            .fmt    = "invalid pipeline state: %s",
            .arg0   = !pl ? "pl==NULL" : "no render passes"
        );

    taa_update(pl);

    struct render_pass *last_pass = list_last_entry(&pl->passes, struct render_pass, entry);
    struct render_pass *pass;
    struct mq *mq = NULL;
    bool stop = false;

    pipeline_debug_begin(pl);

    cerr ret = CERR_OK;
    uint64_t failure_mask = 0;
    size_t idx = 0;

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
        ret = pass_render(pl, pass, mq);
        fbo_done(pass->fbo, pl->width, pl->height);

        if (IS_CERR(ret)) {
            err_cerr(ret, "failed to render pass '%s'\n", pass->name);
            failure_mask |= 1llu << idx;
            idx++;
            continue;
        }

        idx++;
    }

    if (!stop)
        pass = last_pass;

    /* render the last pass to the screen */
    /* the screen pass composites over every pixel: nothing to clear */
    renderer_swapchain_begin(pl->renderer, FBOLOAD_DONTCARE);
    ret = pass_render(pl, pass, mq);
    if (IS_CERR(ret)) {
        failure_mask |= 1ull << idx;
        idx++;
    }

    pipeline_debug_end(pl);

    /*
     * Camera matrices as rendered this frame; TAA reprojects against
     * them next frame. Light-only pipelines have no camera and no TAA.
     */
    if (pl->camera)
        mat4x4_mul(pl->taa.prev_proj_view, pl->camera->view.main.proj_mx,
                   pl->camera->view.main.view_mx);

    /* Ping-pong back buffers now hold this frame's output */
    pl->history_valid = true;

    if (failure_mask) {
        if ((failure_mask & ((1ull << idx) - 1)) == failure_mask)
            return CERR_INVALID_OPERATION_REASON(
                .fmt    = "all %zu passes failed",
                .arg0   = (void *)idx
            );
        else
            return CERR_PARTIAL_FAILURE_REASON(
                .fmt    = "%zu passes failed, mask: %016x",
                .arg0   = (void *)failure_mask
            );
    }

    return CERR_OK;
}
