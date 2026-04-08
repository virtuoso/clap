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
        auto sv = &view->subview[0];
        mat4x4 pv, inv_pv;
        mat4x4_mul(pv, sv->proj_mx, sv->view_mx);
        mat4x4_invert(inv_pv, pv);

        vec4 vs_left, vs_right;
        float ndc_z = renderer_get_caps(params->renderer)->ndc_z_zero_one ? 0.0f : -1.0f;
        mat4x4_mul_vec4_post(vs_left, inv_pv, (vec4) { -1.0, 0.0, ndc_z, 1.0 });
        mat4x4_mul_vec4_post(vs_right, inv_pv, (vec4) { 1.0, 0.0, ndc_z, 1.0 });

        vec3_scale(vs_left, vs_left, 1.0f / vs_left[3]);
        vec3_scale(vs_right, vs_right, 1.0f / vs_right[3]);

        float vs_span = fabsf(vs_right[0] - vs_left[0]) / cos(M_PI_4);
        float width = (float)*pwidth / display_get_scale();
        float texel_size = vs_span / width;
        side = (int)((params->cascade > 0 ? 0.5 : 1.0f) / texel_size);
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

static cresp(render_pass) add_blur_subchain(pipeline *pl, render_pass *src, texture_format format, float scale)
{
    auto pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = src,
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

    auto renderer = clap_get_renderer(opts->pl_opts->clap_ctx);
    static ssao_state ssao_state = {};
    if (ssao)
        ssao_init(renderer, &ssao_state);
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
    unsigned int nr_cascades = min(opts->pl_opts->nr_cascades, CASCADES_MAX) ?: CASCADES_MAX;

    pipeline *pl = opts->pl ? : CRES_RET_T(
        ref_new_checked(
            pipeline,
            .light            = opts->pl_opts->light,
            .camera           = opts->pl_opts->camera,
            .clap_ctx         = opts->pl_opts->clap_ctx,
            .ssao_state       = &ssao_state,
            .nr_cascades      = nr_cascades,
            .name             = opts->pl_opts->name
        ),
        pipeline
    );

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
            .layout             = FBO_COLOR_DEPTH_TEXTURE(RT_MODEL_LAST),
            .name               = "model",
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
                    /* Normal */
                    .format         = TEX_FMT_RGBA8,
                    .load_action    = FBOLOAD_CLEAR,
                },
            },
            .depth_config       = {
                .format         = TEX_FMT_DEPTH32F,
                .load_action    = FBOLOAD_CLEAR,
                .clear_depth    = 1.0,
                .depth_func     = DEPTH_FN_LESS,
            },
        ),
        pipeline
    );
    struct render_pass *pass;
    pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = model_pass,
                    .attachment = FBO_COLOR_TEXTURE(RT_MODEL_EMISSION),
                    .method     = model_pass_method,
                    .sampler    = UNIFORM_MODEL_TEX
                },
                {}
            },
            .color_config       = (fbo_attconfig[]) { { .format = hdr_fmt, .load_action = FBOLOAD_DONTCARE, } },
            .layout             = FBO_COLOR_TEXTURE(0),
            .ops                = &postproc_ops,
            .shader             = "downsample",
            .scale              = 0.25,
        ),
        pipeline
    );

    pass = CRES_RET_T(add_blur_subchain(pl, pass, hdr_fmt, 0.25), pipeline);

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
                    .attachment = FBO_COLOR_TEXTURE(RT_MODEL_EMISSION),
                    .method     = model_pass_method,
                    .sampler    = UNIFORM_EMISSION_MAP
                },
                {}
            },
            .color_config       = (fbo_attconfig[]) { { .format = hdr_fmt, .load_action = FBOLOAD_DONTCARE, } },
            .layout             = FBO_COLOR_TEXTURE(0),
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
                { .tex  = &ssao_state.noise, .method = RM_PLUG, .sampler = UNIFORM_SOBEL_TEX },
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
        ? CRES_RET_T(add_blur_subchain(pl, ssao_pass, TEX_FMT_R8, 0.25), pipeline)
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
                {
                    .tex        = lut_tex(ropts->lighting_lut),
                    .method     = RM_PLUG,
                    .sampler    = UNIFORM_LUT_TEX
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
            .shader             = "combine",
            .checkpoint         = 1,
        ),
        pipeline
    );

    /* Extra blur for the menu */
    pass = CRES_RET_T(
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                {
                    .pass       = combine_pass,
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

    pass = CRES_RET_T(add_blur_subchain(pl, pass, TEX_FMT_RGBA8, 0.25), pipeline);

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

    return cresp_val(pipeline, pl);
}
