// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "memory.h"
#include "model.h"
#include "pipeline.h"
#include "primitives.h"
#include "scene.h"
#include "shader.h"
#include "ui-debug.h"

struct pipeline {
    struct scene        *scene;
    struct ref          ref;
    struct list         passes;
    bool                (*resize)(fbo_t *fbo, bool shadow_map, int w, int h);
    const char          *name;
    renderer_t          *renderer;
};

struct render_pass {
    darray(struct render_pass *, src);
    darray(fbo_t *, fbo);
    darray(int, blit_src);
    struct mq           mq;
    struct list         entry;
    struct render_pass  *repeat;
    struct shader_prog  *prog_override;
    const char          *name;
    int                 rep_total;
    int                 rep_count;
    int                 cascade;
    bool                blit;
    bool                stop;
};

texture_t *pipeline_pass_get_texture(struct render_pass *pass, unsigned int idx)
{
    if (idx >= darray_count(pass->fbo))
        return NULL;
    return fbo_texture(pass->fbo.x[idx]);
}

#define DEFAULT_FBO_SIZE 1024
static inline int shadow_map_size(int width, int height)
{
    int side = max(width, height);
    int order = fls(side);

    if (!order)
        return DEFAULT_FBO_SIZE;

    return 1 << order;
}

static bool pipeline_default_resize(fbo_t *fbo, bool shadow_map, int width, int height)
{
    int side = max(width, height);

    if (side <= 0)
        width = height = DEFAULT_FBO_SIZE;
    else if (shadow_map)
        width = height = shadow_map_size(width, height);

    cerr err = fbo_resize(fbo, width, height);
    return !IS_CERR(err);
}

void pipeline_set_resize_cb(struct pipeline *pl, bool (*cb)(fbo_t *, bool, int, int))
{
    pl->resize = cb ? cb : pipeline_default_resize;
}

static cerr pipeline_make(struct ref *ref, void *_opts)
{
    rc_init_opts(pipeline) *opts = _opts;
    if (!opts->name || !opts->scene)
        return CERR_INVALID_ARGUMENTS;

    struct pipeline *pl = container_of(ref, struct pipeline, ref);
    list_init(&pl->passes);
    pl->renderer = clap_get_renderer(opts->scene->clap_ctx);
    pl->scene = opts->scene;
    pl->name = opts->name;
    pl->resize = pipeline_default_resize;

    return CERR_OK;
}

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
        /*
         * Depth/color attachments don't have quad models,
         * so the below would cause a massive memory violation
         * that for some reason no ASAN can catch, so naturally
         * skip it if the mq list is empty
         */
        if (!list_empty(&pass->mq.txmodels)) {
            model3dtx *txm = mq_model_first(&pass->mq);
            int i;

            for (i = 0; i < darray_count(pass->fbo); i++) {
                fbo_t **pfbo = &pass->fbo.x[i];

                if (txm->emission == fbo_texture(*pfbo))
                    txm->emission = &txm->_emission;
                if (txm->sobel == fbo_texture(*pfbo))
                    txm->sobel = &txm->_sobel;
            }
        }

        darray_clearout(pass->src);
        darray_clearout(pass->blit_src);
    }

    list_for_each_entry_iter(pass, iter, &pl->passes, entry) {
        mq_release(&pass->mq);

        fbo_t **pfbo;
        darray_for_each(pfbo, pass->fbo)
            fbo_put(*pfbo);
        darray_clearout(pass->fbo);

        if (pass->prog_override)
            ref_put(pass->prog_override);

        mem_free(pass);
    }
}
DEFINE_REFCLASS2(pipeline);

void pipeline_put(struct pipeline *pl)
{
    ref_put(pl);
}

struct pipeline *pipeline_new(struct scene *s, const char *name)
{
    return ref_new(pipeline, .name = name, .scene = s);
}

void pipeline_resize(struct pipeline *pl)
{
    struct render_pass *pass;

    list_for_each_entry(pass, &pl->passes, entry) {
        if (pass->prog_override)
            continue;

        fbo_t **pfbo;
        darray_for_each(pfbo, pass->fbo)
            if (!pl->resize(*pfbo, false, pl->scene->width, pl->scene->height))
                return;
    }
}

void pipeline_shadow_resize(struct pipeline *pl, int width)
{
    struct render_pass *pass;

    list_for_each_entry(pass, &pl->passes, entry) {
        if (pass->prog_override)
            goto found;
    }

    return;

found:
    (void)pl->resize(pass->fbo.x[0], true, width, width);
}

static struct render_pass *
__pipeline_add_pass(struct pipeline *pl, struct render_pass *src, const char *shader,
                    const char *shader_override, bool ms, int nr_targets, int target, int cascade)
{
    struct render_pass *pass;
    struct shader_prog *p;
    model3dtx *txm;
    entity3d *e;
    model3d *m;

    pass = mem_alloc(sizeof(*pass), .zero = 1);
    if (!pass)
        return NULL;

    list_append(&pl->passes, &pass->entry);
    darray_init(pass->src);
    darray_init(pass->fbo);
    darray_init(pass->blit_src);
    pass->name = shader;

    /*
     * FBOs and srcs are a 1:1 mapping *except* the ms passes, which don't have
     * sources, and the data is copied out to following passes' textures instead
     * of by rendering a quad
     */
    fbo_t **pfbo = darray_add(pass->fbo);
    if (!pfbo)
        return NULL;

    int width = pl->scene->width, height = pl->scene->height;

    /*
     * XXX: better way of determining shadow pass
     */
    if (shader_override) {
        width = height = shadow_map_size(width, height);
        pass->name = shader_override;
    } else if (src && !src->prog_override) {
        width = fbo_width(src->fbo.x[0]);
        height = fbo_height(src->fbo.x[0]);
    }

#ifdef CONFIG_GLES
    if (nr_targets == FBO_DEPTH_TEXTURE) {
        /* in GL ES, we render one depth cascade at a time */
        pass->cascade = clamp(cascade, 0, CASCADES_MAX - 1);
    } else {
        /* otherwise, render all at once into a texture array */
        pass->cascade = -1;
    }
#else
    pass->cascade = -1;
#endif /* CONFIG_GLES */

    cresp(fbo_t) res = fbo_new(.width          = width,
                               .height         = height,
                               .multisampled   = ms,
                               .nr_attachments = nr_targets);
    if (IS_CERR(res))
        goto err_fbo_array;

    *pfbo = res.val;

    struct render_pass **psrc = darray_add(pass->src);
    if (!psrc)
        goto err_fbo;

    *psrc = src;

    /*
     * nr_targets > 0 means color buffers instead of a texture in the FBO, so
     * the next pass will have to blit from this one instead of rendering it
     */
    pass->blit = nr_targets > 0;
    mq_init(&pass->mq, NULL);

    if (shader_override) {
        pass->prog_override = shader_prog_find(&pl->scene->shaders, shader_override);
        if (!pass->prog_override)
            goto err_src;
    }

    /*
     * nr_targets > 0: ignore @shader, because it means that we'll be blitting
     * from this fbo instead of rendering its quad using a shader
     */
    if (!shader || pass->blit)
        return pass;

    int *pblit_src = darray_add(pass->blit_src);
    if (!pblit_src)
        goto err_override;

    *pblit_src = target;

    p = shader_prog_find(&pl->scene->shaders, shader);
    if (!p)
        goto err_blit_src;

    /* XXX: error checking */
    m = model3d_new_quad(p, -1, 1, 0, 2, -2);
    m->depth_testing = false;
    m->alpha_blend = false;
    /* XXX: error checking */
    txm = ref_new(model3dtx, .model = ref_pass(m), .tex = fbo_texture(*pfbo));
    mq_add_model(&pass->mq, txm);
    /* XXX: error checking */
    e = ref_new(entity3d, .txmodel = txm);
    list_append(&txm->entities, &e->entry);
    e->visible = true;
    mat4x4_identity(e->mx->m);
    ref_put(p);

    return pass;

err_blit_src:
    darray_clearout(pass->blit_src);
err_override:
    if (shader_override)
        ref_put(pass->prog_override);
err_src:
    darray_clearout(pass->src);
err_fbo:
    fbo_put_last(*pfbo);
err_fbo_array:
    darray_clearout(pass->fbo);
    list_del(&pass->entry);
    mem_free(pass);
    return NULL;
}

struct render_pass *_pipeline_add_pass(struct pipeline *pl, const pipeline_pass_config *cfg)
{
    pipeline_pass_config _cfg = *cfg;
    struct render_pass *pass;

    pass = __pipeline_add_pass(pl, _cfg.source, _cfg.shader, _cfg.shader_override, _cfg.multisampled,
                               _cfg.nr_attachments, _cfg.blit_from, _cfg.cascade);
    if (pass) {
        if (_cfg.name)
            pipeline_pass_set_name(pass, _cfg.name);
        if (_cfg.pingpong)
            pipeline_pass_repeat(pass, _cfg.source, _cfg.pingpong);
        pass->stop = _cfg.stop;
    }

    return pass;
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
    model3dtx *txm = mq_model_first(&pass->mq);
    struct render_pass **psrc = darray_add(pass->src);

    if (!psrc)
        return;

    fbo_t **pfbo = darray_add(pass->fbo);
    if (!pfbo)
        goto err_src;

    /* this is where src's txmodel will be rendered */
    cresp(fbo_t) res = fbo_new(pl->scene->width, pl->scene->height);
    if (IS_CERR(res))
        goto err_fbo_array;

    *pfbo = res.val;

    int *pblit_src = darray_add(pass->blit_src);
    if (!pblit_src)
        goto err_fbo;

    if (src->blit)
        *pblit_src = blit_src;

    *psrc = src;
    model3dtx_set_texture(txm, to, fbo_texture(*pfbo));
    return;

err_fbo:
    fbo_put(*pfbo);
err_fbo_array:
    darray_delete(pass->fbo, -1);
err_src:
    darray_delete(pass->src, -1);
}

#ifndef CONFIG_FINAL
static void pipeline_debug_begin(struct pipeline *pl)
{
    debug_module *dbgm = ui_igBegin_name(DEBUG_PIPELINE_PASSES, ImGuiWindowFlags_AlwaysAutoResize,
                                         "pipeline %s", pl->name);

    if (!dbgm->display || !dbgm->unfolded)
        return;

    struct render_pass *pass;
    char dbg_name[128];

    list_for_each_entry(pass, &pl->passes, entry)
        if (pass->rep_total) {
            snprintf(dbg_name, sizeof(dbg_name), "%s reps", pass->name);
            igSliderInt(dbg_name, &pass->rep_total, 1, 20, "%d", ImGuiSliderFlags_AlwaysClamp);
        }

    igBeginTable("pipeline passes", 5, ImGuiTableFlags_Borders, (ImVec2){0,0}, 0);
    igTableSetupColumn("pass", ImGuiTableColumnFlags_WidthStretch, 0, 0);
    igTableSetupColumn("src", ImGuiTableColumnFlags_WidthFixed, 0, 0);
    igTableSetupColumn("dim", ImGuiTableColumnFlags_WidthFixed, 0, 0);
    igTableSetupColumn("attachment", ImGuiTableColumnFlags_WidthFixed, 0, 0);
    igTableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed, 0, 0);
}

static void pipeline_debug_end(struct pipeline *pl)
{
    debug_module *dbgm = ui_debug_module(DEBUG_PIPELINE_PASSES);

    if (!dbgm->display)
        return;
    if (dbgm->unfolded)
        igEndTable();

    ui_igEnd(DEBUG_PIPELINE_PASSES);
}

static void pipeline_pass_debug_begin(struct pipeline *pl, struct render_pass *pass, int srcidx,
                                      struct render_pass *src)
{
    debug_module *dbgm = ui_debug_module(DEBUG_PIPELINE_PASSES);
    fbo_t *fbo = pass->fbo.x[srcidx];

    if (!dbgm->display || !dbgm->unfolded)
        return;
    igTableNextRow(0, 0);
    igTableNextColumn();

    igText("%s:%d", pass->name, srcidx);
    igTableNextColumn();
    if (src) {
        if (src->blit)
            igText("%s:%d", src->name, pass->blit_src.x[srcidx]);
        else
            igText("%s", src->name);
    } else {
        igText("<none>");
    }
    igTableNextColumn();
    igText("%u x %u", fbo_width(fbo), fbo_height(fbo));
    igTableNextColumn();
    const char *att[] = {
        [FBO_ATTACHMENT_COLOR0]  = "color",
        [FBO_ATTACHMENT_DEPTH]   = "depth",
        [FBO_ATTACHMENT_STENCIL] = "stencil",
    };
    igText("%s", att[fbo_get_attachment(fbo)]);
}

static void pipeline_pass_debug_end(struct pipeline *pl, unsigned long count)
{
    debug_module *dbgm = ui_debug_module(DEBUG_PIPELINE_PASSES);
    if (!dbgm->display || !dbgm->unfolded)
        return;

    igTableNextColumn();
    igText("%lu", count);
}
#else
static inline void pipeline_debug_begin(struct pipeline *pl) {}
static inline void pipeline_debug_end(struct pipeline *pl) {}
static inline void pipeline_pass_debug_begin(struct pipeline *pl, struct render_pass *pass, int srcidx,
                                             struct render_pass *src) {}
static inline void pipeline_pass_debug_end(struct pipeline *pl, unsigned long count) {}
#endif /* CONFIG_FINAL */

void pipeline_render(struct pipeline *pl, bool stop)
{
    struct scene *s = pl->scene;
    struct render_pass *last_pass = list_last_entry(&pl->passes, struct render_pass, entry);
    struct render_pass *pass, *ppass = NULL;
    bool repeating = false;

    pipeline_debug_begin(pl);

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
            fbo_t *fbo = pass->fbo.x[i];
            unsigned long count = 0;

            pipeline_pass_debug_begin(pl, pass, i, src);

            if (src && src->blit) {
                fbo_prepare(fbo);
                fbo_blit_from_fbo(fbo, src->fbo.x[0], pass->blit_src.x[i]);
                fbo_done(fbo, s->width, s->height);
            } else {
                fbo_prepare(fbo);
                bool shadow = fbo_get_attachment(fbo) == FBO_ATTACHMENT_DEPTH;
                bool clear_color = true, clear_depth = false;
                if (shadow) {
                    renderer_clearcolor(pl->renderer, (vec4){ 0, 0, 0, 1 });
                    clear_color = false;
                }

                if (!src)
                    clear_depth = true;
                renderer_clear(pl->renderer, clear_color, clear_depth, false);

                if (!src)
                    models_render(pl->renderer, &s->mq, pass->prog_override, &s->light,
                                  shadow ? NULL : &s->cameras[0],
                                  &s->cameras[0].view.main.proj_mx, s->focus, fbo_width(fbo), fbo_height(fbo),
                                  pass->cascade, &count);
                else
                    models_render(pl->renderer, &src->mq, NULL, NULL, NULL, NULL, NULL,
                                  fbo_width(fbo), fbo_height(fbo), -1, &count);

                fbo_done(fbo, s->width, s->height);
            }

            pipeline_pass_debug_end(pl, count);
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

        if (stop && pass->stop)
            break;
    }

    pipeline_debug_end(pl);

    if (!stop)
        pass = last_pass;

    /* render the last pass to the screen */
    renderer_clearcolor(pl->renderer, (vec4){ 0, 0, 0, 1 });
    renderer_clear(pl->renderer, true, true, false);
    models_render(pl->renderer, &pass->mq, NULL, NULL, NULL, NULL, NULL, s->width, s->height, -1, NULL);
}

#ifndef CONFIG_FINAL
static void pipeline_passes_dropdown(struct pipeline *pl, int *item, texture_t **tex)
{
    struct render_pass *pass;
    char name[128];
    int i = 0;

    list_for_each_entry(pass, &pl->passes, entry) {
        int cascade = pass->cascade < 0 ? 0 : (pass->cascade + 1);
        fbo_t **pfbo;
        int j = 0;

        darray_for_each(pfbo, pass->fbo) {
            if (*item == i) {
                snprintf(name, sizeof(name), "%s/%d", pass->name, j + 100 * cascade);
                *tex = fbo_texture(*pfbo);
                goto found;
            }
            i++, j++;
        }
    }

    pass = list_first_entry(&pl->passes, struct render_pass, entry);
    *item = 0;

found:
    if (igBeginCombo("passes", name, ImGuiComboFlags_HeightRegular)) {
        i = 0;
        list_for_each_entry(pass, &pl->passes, entry) {
            int cascade = pass->cascade < 0 ? 0 : (pass->cascade + 1);
            fbo_t **pfbo;
            int j = 0;

            darray_for_each(pfbo, pass->fbo) {
                bool selected = *item == i;

                snprintf(name, sizeof(name), "%s/%d", pass->name, j + 100 * cascade);
                if (igSelectable_Bool(name, selected, selected ? ImGuiSelectableFlags_Highlight : 0, (ImVec2){0, 0})) {
                    igSetItemDefaultFocus();
                    *item = i;
                    *tex = fbo_texture(*pfbo);
                }
                i++, j++;
            }
        }
        igEndCombo();
    }
}

static bool debug_shadow_resize(fbo_t *fbo, bool shadow, int width, int height)
{
    cerr err = fbo_resize(fbo, width, height);
    return !IS_CERR(err);
}

void pipeline_debug(struct pipeline *pl)
{
    debug_module *dbgm = ui_igBegin(DEBUG_PIPELINE_SELECTOR, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display)
        return;

    static int pass_preview;
    unsigned int width, height;
    texture_t *pass_tex = NULL;
    int depth_log2;

    if (!dbgm->unfolded)
        return;

    pipeline_passes_dropdown(pl, &pass_preview, &pass_tex);
    if (pass_tex) {
        texture_get_dimesnions(pass_tex, &width, &height);
        if (!pass_preview) {
            int prev_depth_log2 = depth_log2 = ffs(width) - 1;
            igSliderInt("dim log2", &depth_log2, 8, 16, "%d", 0);
            if (depth_log2 != prev_depth_log2) {
                pipeline_set_resize_cb(pl, debug_shadow_resize);
                pipeline_shadow_resize(pl, 1 << depth_log2);
                pipeline_set_resize_cb(pl, NULL);
            }
            igText("shadow map resolution: %d x %d", 1 << depth_log2, 1 << depth_log2);
        } else {
            igText("texture resolution: %d x %d", width, height);
        }
    }
    ui_igEnd(DEBUG_PIPELINE_SELECTOR);

    if (pass_tex && !texture_is_array(pass_tex) && !texture_is_multisampled(pass_tex)) {
        if (igBegin("Render pass preview", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            igPushItemWidth(512);
            double aspect = (double)height / width;
            igImage((ImTextureID)texture_id(pass_tex), (ImVec2){512, 512 * aspect},
                    (ImVec2){1,1}, (ImVec2){0,0}, (ImVec4){1,1,1,1}, (ImVec4){1,1,1,1});
            igEnd();
        } else {
            igEnd();
        }
    }
}
#endif /* CONFIG_FINAL */
