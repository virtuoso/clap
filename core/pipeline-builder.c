// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "camera.h"
#include "display.h"
#include "light.h"
#include "lut.h"
#include "noise.h"
#include "pipeline.h"
#include "pipeline-builder.h"
#include "render.h"
#include "shader_constants.h"
#include "ssao.h"

/****************************************************************************
 * shadow render pass operations
 ****************************************************************************/

#define DEFAULT_SHADOW_SIZE 1024
#define SHADOW_CAP          4096
#define SHADOW_CASCADE_MIN  512

static bool shadow_resize(render_pass_ops_params *params, unsigned int *pwidth, unsigned int *pheight)
{
    int max_tex = renderer_query_limits(params->renderer, RENDER_LIMIT_MAX_TEXTURE_SIZE);
    int cap = min(SHADOW_CAP, max_tex);

    int screen_side = (int)((float)max(*pwidth, *pheight) / display_get_scale());
    int size = clamp(1 << fls(screen_side - 1), DEFAULT_SHADOW_SIZE, cap);

    *pwidth = *pheight = size;

    return true;
}

static void shadow_prepare(render_pass_ops_params *params)
{
    auto cascade = params->cascade;
    if (cascade >= 0) {
        params->near_plane = params->light->view[0].subview[cascade].near_plane;
        params->far_plane = params->light->view[0].subview[cascade].far_plane;
    } else {
        params->near_plane = params->light->view[0].main.near_plane;
        params->far_plane = params->light->view[0].main.far_plane;
    }
    params->camera = NULL;
}

static const render_pass_ops shadow_ops = {
    .resize     = shadow_resize,
    .prepare    = shadow_prepare,
};

/****************************************************************************
 * model render pass operations
 ****************************************************************************/

static bool model_resize(render_pass_ops_params *params, unsigned int *pwidth, unsigned int *pheight)
{
    *pwidth     = (float)*pwidth * params->render_scale;
    *pheight    = (float)*pheight * params->render_scale;

    return true;
}

static void model_prepare(render_pass_ops_params *params)
{
}

static const render_pass_ops model_ops = {
    .resize     = model_resize,
    .prepare    = model_prepare,
};

/****************************************************************************
 * postprocessing render passes' operations
 ****************************************************************************/

static bool postproc_resize(render_pass_ops_params *params, unsigned int *pwidth, unsigned int *pheight)
{
    *pwidth     = (float)*pwidth * params->render_scale;
    *pheight    = (float)*pheight * params->render_scale;

    return true;
}

static void postproc_prepare(render_pass_ops_params *params)
{
}

static const render_pass_ops postproc_ops = {
    .resize     = postproc_resize,
    .prepare    = postproc_prepare,
};

static inline render_options *get_ropts(pipeline_builder_opts *opts)
{
    return clap_get_render_options(opts->pl_opts->clap_ctx);
}

static void apply_constraints(pipeline_builder_opts *opts)
{
    clap_context *clap_ctx = opts->pl_opts->clap_ctx;
    if (clap_ctx && clap_get_os(clap_ctx)->mobile) {
        /*
         * On iOS and iPadOS as of today, webgl silently doesn't render
         * to non-8bit MRTs. It does render the DEPTH32 by itself though,
         * therefore, switch these targets to CSM shadows.
         */
        get_ropts(opts)->shadow_vsm = false;
    }

    /*
     * TAA compiled out: the model shaders have no velocity RT to write
     * and the taa shader isn't part of the pipeline
     */
    if (!IS_DEFINED(CONFIG_TAA))    get_ropts(opts)->taa = false;
}

static texture_format get_hdr_format(pipeline_builder_opts *opts)
{
    if (!get_ropts(opts)->hdr)
        return TEX_FMT_RGBA8;

    clap_context *clap_ctx = opts->pl_opts->clap_ctx;
    if (clap_ctx && clap_get_os(clap_ctx)->mobile)
        return TEX_FMT_RGBA8;

    texture_format hdr_fmts[] = {
        TEX_FMT_RGB16F, TEX_FMT_RGBA16F, TEX_FMT_RGB32F, TEX_FMT_RGBA32F
    };

    auto r = clap_get_renderer(clap_ctx);
    texture_format hdr_fmt = TEX_FMT_RGBA8;
    for (int i = 0; i < array_size(hdr_fmts); i++)
        if (fbo_texture_supported(r, hdr_fmts[i])) {
            hdr_fmt = hdr_fmts[i];
            break;
        }

    return hdr_fmt;
}

/*
 * The bloom chain carries the emitter's distance in alpha (see
 * bloom-downsample.frag), so its render targets need a format that
 * keeps one: the RGB HDR formats get_hdr_format() prefers for the
 * model pass' bandwidth have no alpha where the backend maps them
 * literally (GL), and a write to it is silently dropped
 */
static texture_format get_bloom_format(pipeline_builder_opts *opts, texture_format hdr_fmt)
{
    texture_format with_alpha;

    switch (hdr_fmt) {
        case TEX_FMT_RGB16F:    with_alpha = TEX_FMT_RGBA16F;   break;
        case TEX_FMT_RGB32F:    with_alpha = TEX_FMT_RGBA32F;   break;
        default:                return hdr_fmt;
    }

    auto r = clap_get_renderer(opts->pl_opts->clap_ctx);
    return fbo_texture_supported(r, with_alpha) ? with_alpha : hdr_fmt;
}

static texture_format get_depth_format(pipeline_builder_opts *opts)
{
    /*
     * Prefer 32-bit float depth: it gives the best precision (the reverse-z
     * shadow passes in particular want float depth), and is measured to be no
     * more expensive than 24-bit on RPi5/V3D -- GL_DEPTH_COMPONENT24 occupies a
     * 32-bit slot there too, so 24-bit buys no bandwidth. Fall back to 24/16-bit
     * only when the renderer can't attach 32F.
     */
    texture_format depth_fmts[] = {
        TEX_FMT_DEPTH32F, TEX_FMT_DEPTH24F, TEX_FMT_DEPTH16F
    };

    auto r = clap_get_renderer(opts->pl_opts->clap_ctx);
    texture_format depth_fmt = TEX_FMT_DEPTH16F;
    for (int i = 0; i < array_size(depth_fmts); i++)
        if (fbo_texture_supported(r, depth_fmts[i])) {
            depth_fmt = depth_fmts[i];
            break;
        }

    return depth_fmt;
}

static cresp(render_pass) add_blur_subchain(pipeline *pl, render_pass *src, fbo_attachment rt, texture_format format,
                                            float scale)
{
    auto pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = src,
                    .attachment = rt,
                    .method     = RM_USE,
                    .sampler    = UNIFORM_MODEL_TEX,
                },
                {}
            },
            .color_config       = (fbo_attconfig[]) { { .format = format, .load_action = FBOLOAD_DONTCARE } },
            .layout             = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            .scale              = scale,
            .shader             = "vblur",
        ),
        render_pass
    );
    pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {}
            },
            .color_config       = (fbo_attconfig[]) { { .format = format, .load_action = FBOLOAD_DONTCARE } },
            .layout             = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            .scale              = scale,
            .shader             = "hblur",
        ),
        render_pass
    );

    return cresp_val(render_pass, pass);
}

/*
 * The head of a bloom chain: the thresholded emission tent-downsampled
 * to @scale, with the emitter distance carried in alpha for the
 * distance lerp in combine_color(), see bloom-downsample.frag
 */
static cresp(render_pass) add_bloom_downsample(pipeline *pl, render_pass *model_pass, render_method method,
                                               texture_format format, float scale)
{
    auto pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = model_pass,
                    .attachment = FBO_COLOR_TEXTURE(RT_MODEL_EMISSION),
                    .method     = method,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {
                    .pass       = model_pass,
                    .attachment = FBO_DEPTH_TEXTURE(0),
                    .method     = method,
                    .sampler    = UNIFORM_DEPTH_TEX
                },
                {}
            },
            .color_config       = (fbo_attconfig[]) { { .format = format, .load_action = FBOLOAD_DONTCARE } },
            .layout             = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            /* unjitters its input: emission pulses across the coarse texels otherwise */
            .shader             = "bloom-downsample",
            .scale              = scale,
        ),
        render_pass
    );

    return cresp_val(render_pass, pass);
}

/****************************************************************************
 * pipeline builder
 ****************************************************************************/

/*
 * Shared by all pipeline (re)builds; released in pipeline_builder_done().
 * TODO: these want proper ownership (the pipeline object or a builder
 * context) instead of hidden statics with an out-of-band destructor
 */
static texture_t grain_tex;
static ssao_state pl_ssao_state;

/**
 * pipeline_builder_done() - release pipeline builder's process-wide resources
 */
void pipeline_builder_done(void)
{
    ssao_done(&pl_ssao_state);
    texture_deinit(&grain_tex);
}

DEFINE_CLEANUP(pipeline, if (*p) ref_put(*p))

cresp(pipeline) pipeline_build(pipeline_builder_opts *opts)
{
    if (!opts->mq)
        return cresp_error(pipeline, CERR_NOMEM);

    if (!texture_loaded(&grain_tex))
        blue_noise2d_tex(clap_get_renderer(opts->pl_opts->clap_ctx), &grain_tex, FILM_GRAIN_SIZE);

    apply_constraints(opts);

    render_options *ropts = get_ropts(opts);
    bool ssao = ropts->ssao;

    /*
     * No LUT in render_options, but one is required; pick the first one
     * from the list, bail if the list is empty
     */
    if (!ropts->lighting_lut)
        ropts->lighting_lut = CRES_RET_T(
            lut_first(clap_lut_list(opts->pl_opts->clap_ctx)),
            pipeline
        );

    auto renderer = clap_get_renderer(opts->pl_opts->clap_ctx);
    if (ssao)   ssao_init(renderer, &pl_ssao_state);
    else        ssao_done(&pl_ssao_state);

    bool taa = ropts->taa;
    bool edges = ropts->edges;
    /* TAA replaces SMAA: no weights pass, use_edge_aa off in combine */
    bool edge_aa = ropts->edge_antialiasing && !taa && edges;
    /*
     * The combine/present split only exists so TAA can resolve (and SMAA
     * blend) between the composite and the OETF encode; with neither in
     * play, one merged screen pass saves a full-res RT write + read
     */
    bool merge_present = !taa && !edge_aa;
    bool model_pass_msaa =
#ifdef CONFIG_BROWSER
        false;
#else
        ropts->model_msaa;
#endif /* CONFIG_BROWSER */

    render_method model_pass_method = model_pass_msaa ? RM_BLIT : RM_USE;

    bool edge_sobel = ropts->edge_sobel;
    const char *edge_msaa_shader = edge_sobel ? "sobel-msaa" : "laplace";
    const char *edge_shader = edge_sobel ? "sobel" : "laplace";
    bool vsm = ropts->shadow_vsm;
    unsigned int nr_cascades = min(opts->pl_opts->nr_cascades, CASCADES_MAX) ?: CASCADES_MAX;

    /*
     * A newly created pipeline is auto-released if any pass setup below
     * fails; a caller-provided opts->pl (rebuild) stays owned by the caller
     */
    LOCAL(pipeline, new_pl);
    if (!opts->pl)
        new_pl = CRES_RET_T(
            ref_new_checked(
                pipeline,
                .light            = opts->pl_opts->light,
                .camera           = opts->pl_opts->camera,
                .clap_ctx         = opts->pl_opts->clap_ctx,
                .ssao_state       = &pl_ssao_state,
                .nr_cascades      = nr_cascades,
                .name             = opts->pl_opts->name
            ),
            pipeline
        );

    pipeline *pl = opts->pl ? : new_pl;

    struct render_pass *shadow_pass[CASCADES_MAX];

#ifndef CONFIG_SHADOW_MAP_ARRAY
    for (unsigned int i = 0; i < nr_cascades; i++) {
        shadow_pass[i] = CRES_RET_T(
            pipeline_add_pass(pl,
                .source               = (render_source[]){ { .mq = opts->mq, .method = RM_RENDER, }, {} },
                .ops                  = &shadow_ops,
                .multisampled         = ropts->shadow_msaa,
                .layout               = vsm ? FBO_COLOR_DEPTH_TEXTURE(0) : FBO_DEPTH_TEXTURE(0),
                .color_config         = (fbo_attconfig[]) {
                    {
                        .format         = TEX_FMT_RG32F,
                        .load_action    = FBOLOAD_CLEAR,
                        .clear_color    = { -1, -1, -1, 1 },
                    }
                },
                .depth_config         = {
                    /* regular shadow maps use 1/z depth; VSM use regular depth */
                    .format           = TEX_FMT_DEPTH32F,
                    .load_action      = FBOLOAD_CLEAR,
                    .store_action     = vsm ? FBOSTORE_DONTCARE : FBOSTORE_STORE,
                    .clear_depth      = vsm ? 1.0 : 0.0,
                    .depth_func       = vsm ? DEPTH_FN_LESS : DEPTH_FN_GREATER,
                },
                .cascade              = i,
                .shader_override      = vsm ? "shadow_vsm" : "shadow"
            ),
            pipeline
        );
        opts->pl_opts->light->shadow[0][i] = pipeline_pass_get_texture(
            shadow_pass[i], vsm ? FBO_COLOR_TEXTURE(0) : FBO_DEPTH_TEXTURE(0)
        );
    }
#else
    shadow_pass[0] = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]){ { .mq = opts->mq, .method = RM_RENDER, }, {} },
            .ops                = &shadow_ops,
            .multisampled       = ropts->shadow_msaa,
            .color_config         = (fbo_attconfig[]) {
                {
                    .format         = TEX_FMT_RG32F,
                    .load_action    = FBOLOAD_CLEAR,
                    .clear_color    = { -1, -1, -1, 1 },
                }
            },
            .depth_config         = {
                /* regular shadow maps use 1/z depth; VSM use regular depth */
                .format           = TEX_FMT_DEPTH32F,
                .load_action      = FBOLOAD_CLEAR,
                .store_action     = vsm ? FBOSTORE_DONTCARE : FBOSTORE_STORE,
                .clear_depth      = vsm ? 1.0 : 0.0,
                .depth_func       = vsm ? DEPTH_FN_LESS : DEPTH_FN_GREATER,
            },
            .layout             = vsm ? FBO_COLOR_DEPTH_TEXTURE(0) : FBO_DEPTH_TEXTURE(0),
            .layers             = nr_cascades,
            .cascade            = -1,
            .shader_override    = vsm ? "shadow_vsm" : "shadow"
        ),
        pipeline
    );
    opts->pl_opts->light->shadow[0][0] = pipeline_pass_get_texture(
        shadow_pass[0], vsm ? FBO_COLOR_TEXTURE(0) : FBO_DEPTH_TEXTURE(0)
    );
#endif /* CONFIG_SHADOW_MAP_ARRAY */

    texture_format hdr_fmt = get_hdr_format(opts);
    texture_format bloom_fmt = get_bloom_format(opts, hdr_fmt);
    texture_format depth_fmt = get_depth_format(opts);

    static const enum shader_vars shadow_samplers[CASCADES_MAX] = {
        UNIFORM_SHADOW_MAP, UNIFORM_SHADOW_MAP1, UNIFORM_SHADOW_MAP2, UNIFORM_SHADOW_MAP3
    };
    unsigned int nr_model_cascades = IS_DEFINED(CONFIG_SHADOW_MAP_ARRAY) ? 1 : nr_cascades;
    render_source model_sources[CASCADES_MAX + 2];
    unsigned int nr_model_sources = 0;

    model_sources[nr_model_sources++] = (render_source) { .mq = opts->mq, .method = RM_RENDER };
    for (unsigned int i = 0; i < nr_model_cascades; i++)
        model_sources[nr_model_sources++] = (render_source) {
            .pass       = shadow_pass[IS_DEFINED(CONFIG_SHADOW_MAP_ARRAY) ? 0 : i],
            .attachment = vsm ? FBO_COLOR_TEXTURE(0) : FBO_DEPTH_TEXTURE(0),
            .method     = RM_USE,
            .sampler    = shadow_samplers[i]
        };
    model_sources[nr_model_sources] = (render_source) {};

    struct render_pass *model_pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = model_sources,
            .multisampled       = model_pass_msaa,
            .ops                = &model_ops,
            .layout             = FBO_COLOR_DEPTH_TEXTURE(
                                    IS_DEFINED(CONFIG_TAA) ? RT_MODEL_LAST : RT_MODEL_VIEW_NORMALS
                                  ),
            .name               = "model",
            .scale              = ropts->render_scale,
            .cascade            = -1,
            .color_config       = (fbo_attconfig[]) {
                [RT_MODEL_LIGHTING] = {
                    /* FragColor */
                    .format         = hdr_fmt,
                    .load_action    = FBOLOAD_CLEAR,
                    .clear_color    = { 0.0f, 0.0f, 0.0f, 1.0f }
                },
                [RT_MODEL_EMISSION] = {
                    /* EmissiveColor */
                    .format         = hdr_fmt,
                    .load_action    = FBOLOAD_CLEAR,
                    .clear_color    = { 0.0f, 0.0f, 0.0f, 0.0f }
                },
                [RT_MODEL_VIEW_NORMALS] = {
                    /* Normal: view-space normal packed in rgb, edge_mask in a;
                     * blending it with alpha_blend models would smear wrong
                     * normals across what's behind, so always overwrite. */
                    .format         = TEX_FMT_RGBA8,
                    .load_action    = FBOLOAD_CLEAR,
                    .no_blend       = true,
                },
#ifdef CONFIG_TAA
                [RT_MODEL_VELOCITY] = {
                    /*
                     * Velocity: TAA motion vectors; cleared to "no motion",
                     * which the resolve reads as "fall back to camera
                     * reprojection". Blending motion vectors is nonsense.
                     */
                    .format         = TEX_FMT_RG16F,
                    .load_action    = FBOLOAD_CLEAR,
                    .no_blend       = true,
                },
#endif /* CONFIG_TAA */
            },
            .depth_config       = {
                .format         = depth_fmt,
                .load_action    = FBOLOAD_CLEAR,
                .clear_depth    = 1.0,
                .depth_func     = DEPTH_FN_LESS,
            },
        ),
        pipeline
    );
    render_pass *bloom_pass = CRES_RET_T(
        add_bloom_downsample(pl, model_pass, model_pass_method, bloom_fmt, 0.25), pipeline
    );

    /*
     * The final bloom blend (blurred chain + raw emission) happens in
     * combine.frag, which samples the quarter-res blur result directly:
     * a separate full-res upsample pass costs a full-res RT write+read
     * for math that amounts to three lines
     */
    render_pass *bloom_blur_pass = CRES_RET_T(
        add_blur_subchain(pl, bloom_pass, FBO_COLOR_TEXTURE(0), bloom_fmt, ropts->render_scale * 0.25),
        pipeline
    );

    /*
     * The tight end of the distance lerp in combine_color(): the same
     * chain again at half res, where its kernel covers half the screen
     * distance. The raw quarter-res downsample is not usable as this
     * end: bilinear magnification of its texel grid shows through as
     * staircases wherever the blend leans on it.
     */
    render_pass *bloom_half_pass = CRES_RET_T(
        add_bloom_downsample(pl, model_pass, model_pass_method, bloom_fmt, 0.5), pipeline
    );
    render_pass *bloom_half_blur_pass = CRES_RET_T(
        add_blur_subchain(pl, bloom_half_pass, FBO_COLOR_TEXTURE(0), bloom_fmt, ropts->render_scale * 0.5), pipeline
    );

    struct render_pass *edge_pass = !edges ? NULL : CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = model_pass,
                    .attachment = FBO_DEPTH_TEXTURE(0),
                    .method     = edge_sobel ? RM_USE : model_pass_method,
                    .sampler    = UNIFORM_DEPTH_TEX
                },
                {
                    .pass       = model_pass,
                    .attachment = FBO_COLOR_TEXTURE(RT_MODEL_VIEW_NORMALS),
                    .method     = edge_sobel ? RM_USE : model_pass_method,
                    .sampler    = UNIFORM_NORMAL_MAP
                },
                {}
            },
            .color_config       = (fbo_attconfig[]) { { .format = TEX_FMT_R8, .load_action = FBOLOAD_DONTCARE, } },
            .layout             = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            .name               = "edge",
            .shader             = model_pass_msaa ? edge_msaa_shader : edge_shader,
            .scale              = ropts->render_scale,
        ),
        pipeline
    );
    render_pass *smaa_weights_pass = edge_aa ? CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = edge_pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = model_pass_method,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {}
            },
            .color_config       = (fbo_attconfig[]) { { .format = TEX_FMT_RGBA8, .load_action = FBOLOAD_DONTCARE, } },
            .ops                = &postproc_ops,
            .layout             = FBO_COLOR_TEXTURE(0),
            .name               = "smaa-weights",
            .shader             = "smaa-blend-weights",
        ),
        pipeline
    ) : NULL;

    render_pass *ssao_pass = ssao ? CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = model_pass,
                    .attachment = FBO_DEPTH_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_DEPTH_TEX
                },
                {
                    .pass       = model_pass,
                    .attachment = FBO_COLOR_TEXTURE(RT_MODEL_VIEW_NORMALS),
                    .method     = model_pass_method,
                    .sampler    = UNIFORM_NORMAL_MAP
                },
                { .tex  = &pl_ssao_state.noise, .method = RM_PLUG, .sampler = UNIFORM_SOBEL_TEX },
                {}
            },
            .color_config       = (fbo_attconfig[]) { { .format = TEX_FMT_R8, .load_action = FBOLOAD_DONTCARE, } },
            .ops                = &postproc_ops,
            .layout             = FBO_COLOR_TEXTURE(0),
            .shader             = "ssao",
            .scale              = 0.25,
        ),
        pipeline
    ) : NULL;

    render_pass *ssao_blur_pass = ssao
        ? CRES_RET_T(add_blur_subchain(pl, ssao_pass, FBO_COLOR_TEXTURE(0), TEX_FMT_R8, 0.25), pipeline)
        : NULL;

    render_pass *combine_pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source            = (render_source[]) {
                {
                    .pass       = model_pass,
                    .attachment = FBO_COLOR_TEXTURE(RT_MODEL_LIGHTING),
                    .method     = model_pass_method,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {
                    .pass       = model_pass,
                    .attachment = FBO_COLOR_TEXTURE(RT_MODEL_EMISSION),
                    .method     = model_pass_method,
                    .sampler    = UNIFORM_EMISSION_MAP
                },
                {
                    .pass       = bloom_blur_pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_BLOOM_TEX
                },
                {
                    .pass       = bloom_half_blur_pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_BLOOM_HALF_TEX
                },
                edges ?
                    (render_source) {
                        .pass       = edge_pass,
                        .attachment = FBO_COLOR_TEXTURE(0),
                        .method     = RM_USE,
                        .sampler    = UNIFORM_SOBEL_TEX
                    } :
                    /* apply_edge() darkens by 1 - edge, white is neutral */
                    (render_source) {
                        .tex        = white_pixel(),
                        .method     = RM_PLUG,
                        .sampler    = UNIFORM_SOBEL_TEX
                    },
                {
                    .pass       = model_pass,
                    .attachment = FBO_DEPTH_TEXTURE(0),
                    .method     = model_pass_method,
                    .sampler    = UNIFORM_DEPTH_TEX
                },
                ssao ?
                    (render_source) {
                        .pass       = ssao_blur_pass,
                        .attachment = FBO_COLOR_TEXTURE(0),
                        .method     = RM_USE,
                        .sampler    = UNIFORM_SHADOW_MAP
                    } :
                    (render_source) {
                        .tex        = black_pixel(),
                        .method     = RM_PLUG,
                        .sampler    = UNIFORM_SHADOW_MAP
                    },
                {
                    .tex        = lut_tex(ropts->lighting_lut),
                    .method     = RM_PLUG,
                    .sampler    = UNIFORM_LUT_TEX
                },
                {
                    .tex        = opts->pl_opts->noise3d,
                    .method     = RM_PLUG,
                    .sampler    = UNIFORM_NOISE3D_TEX
                },
                /* the merged pass applies film grain, see present below */
                merge_present ?
                    (render_source) {
                        .tex        = &grain_tex,
                        .method     = RM_PLUG,
                        .sampler    = UNIFORM_GRAIN_TEX,
                    } :
                    (render_source) {},
                {}
            },
            /*
             * Linear tonemapped color: film grain and the OETF encode
             * happen in the present pass, TAA resolves in between; the
             * merged pass encodes right here and renders to the screen
             * (checkpoint 1), so its FBO (only rendered for the menu
             * blur chain) holds encoded output like present's
             */
#ifdef CONFIG_RENDERER_METAL
            .color_config       = (fbo_attconfig[]) { { .format = hdr_fmt, .load_action = FBOLOAD_DONTCARE, } },
#else
            .color_config       = (fbo_attconfig[]) {
                { .format = merge_present ? TEX_FMT_RGBA8 : hdr_fmt, .load_action = FBOLOAD_DONTCARE, }
            },
#endif /* !CONFIG_RENDERER_METAL */
            .ops                = &postproc_ops,
            .layout             = FBO_COLOR_TEXTURE(0),
            .name               = "combine",
            .shader             = merge_present ? "combine-present" : "combine",
            .checkpoint         = merge_present ? 1 : 0,
        ),
        pipeline
    );

    render_pass *taa_pass = taa ? CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = combine_pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {
                    .pass       = model_pass,
                    .attachment = FBO_DEPTH_TEXTURE(0),
                    .method     = model_pass_method,
                    .sampler    = UNIFORM_DEPTH_TEX
                },
                {
                    .pass       = model_pass,
                    .attachment = FBO_COLOR_TEXTURE(RT_MODEL_VELOCITY),
                    .method     = model_pass_method,
                    .sampler    = UNIFORM_VELOCITY_TEX
                },
                {
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_HISTORY,
                    .sampler    = UNIFORM_HISTORY_TEX
                },
                {}
            },
            .color_config       = (fbo_attconfig[]) { { .format = hdr_fmt, .load_action = FBOLOAD_DONTCARE, } },
            .ops                = &postproc_ops,
            .layout             = FBO_COLOR_TEXTURE(0),
            .shader             = "taa",
            .ping_pong          = true,
        ),
        pipeline
    ) : NULL;

    render_pass *present_pass = merge_present ? combine_pass : CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = taa ? taa_pass : combine_pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {
                    .pass       = model_pass,
                    .attachment = FBO_DEPTH_TEXTURE(0),
                    .method     = model_pass_method,
                    .sampler    = UNIFORM_DEPTH_TEX
                },
                {
                    .tex        = &grain_tex,
                    .method     = RM_PLUG,
                    .sampler    = UNIFORM_GRAIN_TEX,
                },
                /* SMAA blends the composite here; see present.frag */
                edge_aa ?
                    (render_source) {
                        .pass       = edge_pass,
                        .attachment = FBO_COLOR_TEXTURE(0),
                        .method     = RM_USE,
                        .sampler    = UNIFORM_SOBEL_TEX
                    } :
                    (render_source) {
                        .tex        = white_pixel(),
                        .method     = RM_PLUG,
                        .sampler    = UNIFORM_SOBEL_TEX
                    },
                edge_aa ?
                    (render_source) {
                        .pass       = smaa_weights_pass,
                        .attachment = FBO_COLOR_TEXTURE(0),
                        .method     = RM_USE,
                        .sampler    = UNIFORM_SHADOW_MAP2
                    } :
                    (render_source) {
                        .tex        = white_pixel(),
                        .method     = RM_PLUG,
                        .sampler    = UNIFORM_SHADOW_MAP2
                    },
                {}
            },
#ifdef CONFIG_RENDERER_METAL
            .color_config       = (fbo_attconfig[]) { { .format = hdr_fmt, .load_action = FBOLOAD_DONTCARE, } },
#else
            .color_config       = (fbo_attconfig[]) { { .format = TEX_FMT_RGBA8, .load_action = FBOLOAD_DONTCARE, } },
#endif /* !CONFIG_RENDERER_METAL*/
            .ops                = &postproc_ops,
            .layout             = FBO_COLOR_TEXTURE(0),
            .shader             = "present",
            .checkpoint         = 1,
        ),
        pipeline
    );

    /* Extra blur for the menu */
    render_pass *pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = present_pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {}
            },
            .color_config       = (fbo_attconfig[]) { { .format = TEX_FMT_RGBA8, .load_action = FBOLOAD_DONTCARE, } },
            .layout             = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            .shader             = "downsample",
            .scale              = 0.25,
        ),
        pipeline
    );

    pass = CRES_RET_T(add_blur_subchain(pl, pass, FBO_COLOR_TEXTURE(0), TEX_FMT_RGBA8, 0.25), pipeline);

    pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {}
            },
            .color_config       = (fbo_attconfig[]) { { .format = TEX_FMT_RGBA8, .load_action = FBOLOAD_DONTCARE, } },
            .layout             = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            .shader             = "contrast",
            .checkpoint         = 2,
        ),
        pipeline
    );

    return cresp_val(pipeline, opts->pl ? : NOCU(new_pl));
}
