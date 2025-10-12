// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "camera.h"
#include "display.h"
#include "light.h"
#include "lut.h"
#include "pipeline.h"
#include "pipeline-builder.h"
#include "render.h"
#include "shader_constants.h"
#include "ssao.h"

/****************************************************************************
 * shadow render pass operations
 ****************************************************************************/

#define DEFAULT_SHADOW_SIZE 1024
static bool shadow_resize(render_pass_ops_params *params, unsigned int *pwidth, unsigned int *pheight)
{
    int side = max(*pwidth, *pheight);

    if (params->camera) {
        struct view *view = &params->camera->view;
        struct subview *sv = &view->main;
        float c0_depth = view->divider[0] - sv->near_plane;
        float fov_tan = tanf(view->fov / 2);
        float ws_width = 2.0 * c0_depth * fov_tan;
        float width = (float)*pwidth / display_get_scale();
        float texel_size = ws_width / width;
        side = (int)((ws_width / cos(M_PI_4)) / texel_size);
    }

    if (*pwidth == *pheight && !(*pwidth & (*pwidth - 1)))
        return true;

    int order = fls(side);

    *pwidth = *pheight = clamp(1 << order, DEFAULT_SHADOW_SIZE,
        renderer_query_limits(params->renderer, RENDER_LIMIT_MAX_TEXTURE_SIZE)
    );

    return true;
}

static void shadow_prepare(render_pass_ops_params *params)
{
    renderer_cleardepth(params->renderer, 0.0);
    renderer_depth_func(params->renderer, DEPTH_FN_GREATER);
    renderer_clear(params->renderer, false, true, false);
    params->camera = NULL;
}

static const render_pass_ops shadow_ops = {
    .resize     = shadow_resize,
    .prepare    = shadow_prepare,
};

static void shadow_vsm_prepare(render_pass_ops_params *params)
{
    /*
     * VSM needs the depth values to not be reversed, so that gl_FragCoords.z
     * that gets written into shadow maps does not require additional reversal
     * in sampling and to avoid potential precision issues.
     *
     * Unlike the "regular" CSM shadow pass, it also uses a color attachment
     * for its output, which needs to be cleared.qs
     */
    renderer_cleardepth(params->renderer, 1.0);
    renderer_depth_func(params->renderer, DEPTH_FN_LESS);
    renderer_clearcolor(params->renderer, (vec4){ 0, 0, 0, 1 });
    renderer_clear(params->renderer, true, true, false);
    params->camera = NULL;
}

static const render_pass_ops shadow_vsm_ops = {
    .resize     = shadow_resize,
    .prepare    = shadow_vsm_prepare,
};

/****************************************************************************
 * model render pass operations
 ****************************************************************************/

static bool model_resize(render_pass_ops_params *params, unsigned int *pwidth, unsigned int *pheight)
{
    return true;
}

static void model_prepare(render_pass_ops_params *params)
{
    renderer_cleardepth(params->renderer, 1.0);
    renderer_depth_func(params->renderer, DEPTH_FN_LESS);
    renderer_clearcolor(params->renderer, (vec4){ 0, 0, 0, 1 });
    renderer_clear(params->renderer, true, true, false);
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
    texture_format hdr_fmt = TEX_FMT_RGBA8;
    for (int i = 0; i < array_size(hdr_fmts); i++)
        if (fbo_texture_supported(hdr_fmts[i])) {
            hdr_fmt = hdr_fmts[i];
            break;
        }

    return hdr_fmt;
}

/****************************************************************************
 * pipeline builder
 ****************************************************************************/

cresp(pipeline) pipeline_build(pipeline_builder_opts *opts)
{
    if (!opts->mq)
        return cresp_error(pipeline, CERR_NOMEM);

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

    static ssao_state ssao_state = {};
    if (ssao)
        ssao_init(&ssao_state);
    else
        ssao_done(&ssao_state);

    bool edge_aa = ropts->edge_antialiasing;
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

    pipeline *pl = opts->pl ? : CRES_RET_T(
        ref_new_checked(
            pipeline,
            .width            = opts->pl_opts->width,
            .height           = opts->pl_opts->height,
            .light            = opts->pl_opts->light,
            .camera           = opts->pl_opts->camera,
            .clap_ctx         = opts->pl_opts->clap_ctx,
            .ssao_state       = &ssao_state,
            .name             = opts->pl_opts->name
        ),
        pipeline
    );

    struct render_pass *shadow_pass[CASCADES_MAX];

#ifndef CONFIG_SHADOW_MAP_ARRAY
    for (int i = 0; i < CASCADES_MAX; i++) {
        shadow_pass[i] = CRES_RET_T(
            pipeline_add_pass(pl,
                .source               = (render_source[]){ { .mq = opts->mq, .method = RM_RENDER, }, {} },
                .ops                  = vsm ? &shadow_vsm_ops : &shadow_ops,
                .multisampled         = ropts->shadow_msaa,
                .attachment_config    = vsm ? FBO_COLOR_DEPTH_TEXTURE(0) : FBO_DEPTH_TEXTURE(0),
                .color_format         = (texture_format[]) { TEX_FMT_RG32F },
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
            .ops                = vsm ? &shadow_vsm_ops : &shadow_ops,
            .multisampled       = ropts->shadow_msaa,
            .color_format       = (texture_format[]) { TEX_FMT_RG32F },
            .attachment_config  = vsm ? FBO_COLOR_DEPTH_TEXTURE(0) : FBO_DEPTH_TEXTURE(0),
            .layers             = CASCADES_MAX,
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

    struct render_pass *model_pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                { .mq = opts->mq, .method = RM_RENDER, },
#ifndef CONFIG_SHADOW_MAP_ARRAY
                {
                    .pass       = shadow_pass[0],
                    .attachment = vsm ? FBO_COLOR_TEXTURE(0) : FBO_DEPTH_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_SHADOW_MAP
                },
                {
                    .pass       = shadow_pass[1],
                    .attachment = vsm ? FBO_COLOR_TEXTURE(0) : FBO_DEPTH_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_SHADOW_MAP1
                },
                {
                    .pass       = shadow_pass[2],
                    .attachment = vsm ? FBO_COLOR_TEXTURE(0) : FBO_DEPTH_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_SHADOW_MAP2
                },
                {
                    .pass       = shadow_pass[3],
                    .attachment = vsm ? FBO_COLOR_TEXTURE(0) : FBO_DEPTH_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_SHADOW_MAP3
                },
                {}
#else
                {
                    .pass       = shadow_pass[0],
                    .attachment = vsm ? FBO_COLOR_TEXTURE(0) : FBO_DEPTH_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_SHADOW_MAP
                },
                {}
#endif /* CONFIG_SHADOW_MAP_ARRAY */
            },
            .multisampled       = model_pass_msaa,
            .ops                = &model_ops,
            .attachment_config  = FBO_COLOR_DEPTH_TEXTURE(5),
            .name               = "model",
            .cascade            = -1,
            .color_format       = (texture_format[]) {
                                    /* FragColor */     hdr_fmt,
                                    /* EmissiveColor */ hdr_fmt,
                                    /* EdgeNormal */    TEX_FMT_RGBA8,
                                    /* EdgeDepthMask */ TEX_FMT_R32F,
                                    /* ViewPosition */  hdr_fmt,
                                    /* Normal */        TEX_FMT_RGBA8,
                                },
            .depth_format       = TEX_FMT_DEPTH32F
        ),
        pipeline
    );
    struct render_pass *pass;
    pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = model_pass,
                    .attachment = FBO_COLOR_TEXTURE(1),
                    .method     = model_pass_method,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {}
            },
            .color_format       = (texture_format[]) { hdr_fmt },
            .attachment_config  = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            .shader             = "downsample",
            .scale              = 0.25,
        ),
        pipeline
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
            .color_format       = (texture_format[]) { hdr_fmt },
            .attachment_config  = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            .scale              = 0.25,
            .shader             = "vblur",
        ),
        pipeline
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
            .color_format       = (texture_format[]) { hdr_fmt },
            .attachment_config  = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            .scale              = 0.25,
            .shader             = "hblur",
        ),
        pipeline
    );
    struct render_pass *bloom_pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {
                    .pass       = model_pass,
                    .attachment = FBO_COLOR_TEXTURE(1),
                    .method     = model_pass_method,
                    .sampler    = UNIFORM_EMISSION_MAP
                },
                {}
            },
            .color_format       = (texture_format[]) { hdr_fmt },
            .attachment_config  = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            .shader             = "upsample",
        ),
        pipeline
    );
    struct render_pass *edge_pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = model_pass,
                    .attachment = FBO_COLOR_TEXTURE(3),
                    .method     = edge_sobel ? RM_USE : model_pass_method,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {
                    .pass       = model_pass,
                    .attachment = FBO_COLOR_TEXTURE(2),
                    .method     = edge_sobel ? RM_USE : model_pass_method,
                    .sampler    = UNIFORM_NORMAL_MAP
                },
                {}
            },
            .color_format       = (texture_format[]) { TEX_FMT_R8 },
            .attachment_config  = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            .name               = "edge",
            .shader             = model_pass_msaa ? edge_msaa_shader : edge_shader,
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
            .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
            .ops                = &postproc_ops,
            .attachment_config  = FBO_COLOR_TEXTURE(0),
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
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {
                    .pass       = model_pass,
                    .attachment = FBO_COLOR_TEXTURE(5),
                    .method     = model_pass_method,
                    .sampler    = UNIFORM_NORMAL_MAP
                },
                { .tex  = &ssao_state.noise, .method = RM_PLUG, .sampler = UNIFORM_SOBEL_TEX },
                {}
            },
            .color_format       = (texture_format[]) { TEX_FMT_R8 },
            .ops                = &postproc_ops,
            .attachment_config  = FBO_COLOR_TEXTURE(0),
            .shader             = "ssao",
        ),
        pipeline
    ) : NULL;

    render_pass *ssao_vblur_pass = ssao ? CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = ssao_pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {}
            },
            .color_format       = (texture_format[]) { TEX_FMT_R8 },
            .ops                = &postproc_ops,
            .attachment_config  = FBO_COLOR_TEXTURE(0),
            .shader             = "vblur",
            .scale              = 0.25,
        ),
        pipeline
    ) : NULL;

    render_pass *ssao_hblur_pass = ssao ? CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = ssao_vblur_pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {}
            },
            .color_format       = (texture_format[]) { TEX_FMT_R8 },
            .ops                = &postproc_ops,
            .attachment_config  = FBO_COLOR_TEXTURE(0),
            .shader             = "hblur",
            .scale              = 0.25,
        ),
        pipeline
    ) : NULL;

    render_pass *combine_pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source            = (render_source[]) {
                {
                    .pass       = model_pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = model_pass_method,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {
                    .pass       = bloom_pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_EMISSION_MAP
                },
                {
                    .pass       = edge_pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_SOBEL_TEX
                },
                {
                    .pass       = model_pass,
                    .attachment = FBO_COLOR_TEXTURE(4),
                    .method     = model_pass_method,
                    .sampler    = UNIFORM_NORMAL_MAP
                },
                ssao ?
                    (render_source) {
                        .pass       = ssao_hblur_pass,
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
                {}
            },
            .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
            .ops                = &postproc_ops,
            .attachment_config  = FBO_COLOR_TEXTURE(0),
            .shader             = "combine",
        ),
        pipeline
    );

    render_pass *smaa_blend_pass = edge_aa ? CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = combine_pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {
                    .pass       = smaa_weights_pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_NORMAL_MAP
                },
                {}
            },
            .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
            .ops                = &postproc_ops,
            .attachment_config  = FBO_COLOR_TEXTURE(0),
            .name               = "smaa-blend",
            .shader             = "smaa-neighborhood-blend",
        ),
        pipeline
    ) : NULL;

    render_pass *contrast_pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = edge_aa ? smaa_blend_pass : combine_pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {}
            },
            .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
            .ops                = &postproc_ops,
            .attachment_config  = FBO_COLOR_TEXTURE(0),
            .shader             = "contrast",
            .checkpoint         = 1,
        ),
        pipeline
    );

    /* Extra blur for the menu */
    pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = contrast_pass,
                    .attachment = FBO_COLOR_TEXTURE(0),
                    .method     = RM_USE,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {}
            },
            .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
            .attachment_config  = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            .shader             = "downsample",
            .scale              = 0.25,
        ),
        pipeline
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
            .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
            .attachment_config  = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            .scale              = 0.25,
            .shader             = "vblur",
        ),
        pipeline
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
            .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
            .attachment_config  = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            .scale              = 0.25,
            .shader             = "hblur",
        ),
        pipeline
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
            .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
            .attachment_config  = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            .shader             = "contrast",
            .checkpoint         = 2,
        ),
        pipeline
    );

    return cresp_val(pipeline, pl);
}
