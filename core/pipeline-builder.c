// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "camera.h"
#include "light.h"
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
    if (*pwidth == *pheight && !(*pwidth & (*pwidth - 1)))
        return true;

    int side = max(*pwidth, *pheight);
    int order = fls(side);

    if (!order)
        order = DEFAULT_SHADOW_SIZE;
    *pwidth = *pheight = 1 << order;

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

char *__user_agent;

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE __unused void set_user_agent(const char *user_agent)
{
    __user_agent = strdup(user_agent);
    dbg("user agent: '%s'\n", __user_agent);
}
#endif /* __EMSCRIPTEN__ */

static void hdr_constraints(texture_format *hdr_fmt)
{
#ifdef __EMSCRIPTEN__
    EM_ASM(
        ccall("set_user_agent", 'void', ['string'], [navigator.userAgent]);
    );
#endif /* __EMSCRIPTEN__ */

    if (__user_agent && (strstr(__user_agent, "iPhone") || strstr(__user_agent, "iPad")))
        *hdr_fmt = TEX_FMT_RGBA8;
}

static texture_format get_hdr_format(pipeline_builder_opts *opts)
{
    if (!opts->pl_opts->render_options->hdr)
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
    hdr_constraints(&hdr_fmt);

    return hdr_fmt;
}

/****************************************************************************
 * pipeline builder
 ****************************************************************************/

pipeline *pipeline_build(pipeline_builder_opts *opts)
{
    if (!opts->mq)
        return NULL;

    bool ssao = opts->pl_opts->render_options->ssao;

    static ssao_state ssao_state = {};
    if (ssao)
        ssao_init(&ssao_state);
    else
        ssao_done(&ssao_state);

    bool edge_aa = opts->pl_opts->render_options->edge_antialiasing;
    bool model_pass_msaa =
#ifdef CONFIG_BROWSER
        false;
#else
        opts->pl_opts->render_options->model_msaa;
#endif /* CONFIG_BROWSER */

    render_method model_pass_method = model_pass_msaa ? RM_BLIT : RM_USE;

    bool edge_sobel = opts->pl_opts->render_options->edge_sobel;
    const char *edge_msaa_shader = edge_sobel ? "sobel-msaa" : "laplace";
    const char *edge_shader = edge_sobel ? "sobel" : "laplace";

    pipeline *pl = opts->pl ? : ref_new(pipeline,
                           .width            = opts->pl_opts->width,
                           .height           = opts->pl_opts->height,
                           .light            = opts->pl_opts->light,
                           .camera           = opts->pl_opts->camera,
                           .renderer         = opts->pl_opts->renderer,
                           .render_options   = opts->pl_opts->render_options,
                           .shader_ctx       = opts->pl_opts->shader_ctx,
                           .ssao_state       = &ssao_state,
                           .name             = opts->pl_opts->name);

    struct render_pass *shadow_pass[CASCADES_MAX];

#ifdef CONFIG_GLES
    for (int i = 0; i < CASCADES_MAX; i++) {
        shadow_pass[i] =
            pipeline_add_pass(pl,
                .source               = (render_source[]){ { .mq = opts->mq, .method = RM_RENDER, }, {} },
                .ops                  = &shadow_ops,
                .multisampled         = opts->pl_opts->render_options->shadow_msaa,
                .attachment_config    = FBO_DEPTH_TEXTURE(0),
                .cascade              = i,
                .shader_override      = "shadow"
        );
        opts->pl_opts->light->shadow[0][i] = pipeline_pass_get_texture(shadow_pass[i], FBO_DEPTH_TEXTURE(i));
    }
#else
    shadow_pass[0] =
        pipeline_add_pass(pl,
            .source             = (render_source[]){ { .mq = opts->mq, .method = RM_RENDER, }, {} },
            .ops                = &shadow_ops,
            .multisampled       = opts->pl_opts->render_options->shadow_msaa,
            .attachment_config  = FBO_DEPTH_TEXTURE(0),
            .layers             = CASCADES_MAX,
            .cascade            = -1,
            .shader_override    = "shadow"
    );
    opts->pl_opts->light->shadow[0][0] = pipeline_pass_get_texture(shadow_pass[0], FBO_DEPTH_TEXTURE(0));
#endif /* CONFIG_GLES */

    texture_format hdr_fmt = get_hdr_format(opts);

    struct render_pass *model_pass =
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                { .mq = opts->mq, .method = RM_RENDER, },
#ifdef CONFIG_GLES
                { .pass = shadow_pass[0], .attachment = FBO_DEPTH_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_SHADOW_MAP },
                { .pass = shadow_pass[1], .attachment = FBO_DEPTH_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_SHADOW_MAP1 },
                { .pass = shadow_pass[2], .attachment = FBO_DEPTH_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_SHADOW_MAP2 },
                { .pass = shadow_pass[3], .attachment = FBO_DEPTH_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_SHADOW_MAP3 },
                {}
#else
                { .pass = shadow_pass[0], .attachment = FBO_DEPTH_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_SHADOW_MAP }, {}
#endif /* CONFIG_GLES */
            },
            .multisampled       = model_pass_msaa,
            .ops                = &model_ops,
            .attachment_config  = FBO_COLOR_DEPTH_TEXTURE(5),
            .name               = "model",
            .cascade            = -1,
            .color_format       = (texture_format[]) {
                                    hdr_fmt,
                                    hdr_fmt,
                                    TEX_FMT_RGBA8,
                                    TEX_FMT_R32F,
                                    hdr_fmt,
                                    TEX_FMT_RGBA8,
                                },
            .depth_format       = TEX_FMT_DEPTH32F
    );
    struct render_pass *pass;
    pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = model_pass, .attachment = FBO_COLOR_TEXTURE(1), .method = model_pass_method, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]){ hdr_fmt },
        .attachment_config  = FBO_COLOR_TEXTURE(0),
        .ops                = &postproc_ops,
        .shader             = "downsample",
        .scale              = 0.25,
    );

    pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = pass, .attachment = FBO_COLOR_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]){ hdr_fmt },
        .attachment_config  = FBO_COLOR_TEXTURE(0),
        .ops                = &postproc_ops,
        .scale              = 0.25,
        .shader             = "vblur",
    );
    pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = pass, .attachment = FBO_COLOR_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]){ hdr_fmt },
        .attachment_config  = FBO_COLOR_TEXTURE(0),
        .ops                = &postproc_ops,
        .scale              = 0.25,
        .shader             = "hblur",
    );
    struct render_pass *bloom_pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = pass, .attachment = FBO_COLOR_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            { .pass = model_pass, .attachment = FBO_COLOR_TEXTURE(1), .method = model_pass_method, .sampler = UNIFORM_EMISSION_MAP },
            {}
        },
        .color_format       = (texture_format[]) { hdr_fmt },
        .attachment_config  = FBO_COLOR_TEXTURE(0),
        .ops                = &postproc_ops,
        .shader             = "upsample",
    );
    struct render_pass *edge_pass = pipeline_add_pass(pl,
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
    );
    render_pass *smaa_weights_pass = edge_aa ? pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = edge_pass, .attachment = FBO_COLOR_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
        .ops                = &postproc_ops,
        .attachment_config  = FBO_COLOR_TEXTURE(0),
        .name               = "smaa-weights",
        .shader             = "smaa-blend-weights",
    ) : NULL;

    render_pass *ssao_pass = ssao ? pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = model_pass, .attachment = FBO_DEPTH_TEXTURE(0), .method = model_pass_method, .sampler = UNIFORM_MODEL_TEX },
            { .pass = model_pass, .attachment = FBO_COLOR_TEXTURE(5), .method = model_pass_method, .sampler = UNIFORM_NORMAL_MAP },
            { .tex  = &ssao_state.noise, .method = RM_PLUG, .sampler = UNIFORM_SOBEL_TEX },
            {}
        },
        .color_format       = (texture_format[]) { TEX_FMT_R8 },
        .ops                = &postproc_ops,
        .attachment_config  = FBO_COLOR_TEXTURE(0),
        .shader             = "ssao",
    ) : NULL;

    render_pass *ssao_vblur_pass = ssao ? pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = ssao_pass, .attachment = FBO_COLOR_TEXTURE(0), .method = model_pass_method, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]) { TEX_FMT_R8 },
        .ops                = &postproc_ops,
        .attachment_config  = FBO_COLOR_TEXTURE(0),
        .shader             = "vblur",
        .scale              = 0.25,
    ) : NULL;

    render_pass *ssao_hblur_pass = ssao ? pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = ssao_vblur_pass, .attachment = FBO_COLOR_TEXTURE(0), .method = model_pass_method, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]) { TEX_FMT_R8 },
        .ops                = &postproc_ops,
        .attachment_config  = FBO_COLOR_TEXTURE(0),
        .shader             = "hblur",
        .scale              = 0.25,
    ) : NULL;

    render_pass *combine_pass = pipeline_add_pass(pl,
        .source            = (render_source[]) {
            { .pass = model_pass, .attachment = FBO_COLOR_TEXTURE(0), .method = model_pass_method, .sampler = UNIFORM_MODEL_TEX },
            { .pass = bloom_pass, .attachment = FBO_COLOR_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_EMISSION_MAP },
            { .pass = edge_pass, .attachment = FBO_COLOR_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_SOBEL_TEX },
            { .pass = model_pass, .attachment = FBO_COLOR_TEXTURE(4), .method = model_pass_method, .sampler = UNIFORM_NORMAL_MAP },
            ssao ?
                (render_source){ .pass = ssao_hblur_pass, .attachment = FBO_COLOR_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_SHADOW_MAP } :
                (render_source){ .tex = black_pixel(), .method = RM_PLUG, .sampler = UNIFORM_SHADOW_MAP },
            {}
        },
        .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
        .ops                = &postproc_ops,
        .attachment_config  = FBO_COLOR_TEXTURE(0),
        .shader             = "combine",
    );

    render_pass *smaa_blend_pass = edge_aa ? pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = combine_pass, .attachment = FBO_COLOR_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            { .pass = smaa_weights_pass, .attachment = FBO_COLOR_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_NORMAL_MAP },
            {}
        },
        .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
        .ops                = &postproc_ops,
        .attachment_config  = FBO_COLOR_TEXTURE(0),
        .name               = "smaa-blend",
        .shader             = "smaa-neighborhood-blend",
    ) : NULL;

    render_pass *contrast_pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = edge_aa ? smaa_blend_pass : combine_pass, .attachment = FBO_COLOR_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
        .ops                = &postproc_ops,
        .attachment_config  = FBO_COLOR_TEXTURE(0),
        .shader             = "contrast",
        .checkpoint         = 1,
    );

    /* Extra blur for the menu */
    pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = contrast_pass, .attachment = FBO_COLOR_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]){ TEX_FMT_RGBA8 },
        .attachment_config  = FBO_COLOR_TEXTURE(0),
        .ops                = &postproc_ops,
        .shader             = "downsample",
        .scale              = 0.25,
    );
    pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = pass, .attachment = FBO_COLOR_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]){ TEX_FMT_RGBA8 },
        .attachment_config  = FBO_COLOR_TEXTURE(0),
        .ops                = &postproc_ops,
        .scale              = 0.25,
        .shader             = "vblur",
    );
    pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = pass, .attachment = FBO_COLOR_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]){ TEX_FMT_RGBA8 },
        .attachment_config  = FBO_COLOR_TEXTURE(0),
        .ops                = &postproc_ops,
        .scale              = 0.25,
        .shader             = "hblur",
    );
    pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = pass, .attachment = FBO_COLOR_TEXTURE(0), .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
        .attachment_config  = FBO_COLOR_TEXTURE(0),
        .ops                = &postproc_ops,
        .shader             = "contrast",
        .checkpoint         = 2,
    );

    return pl;
}
