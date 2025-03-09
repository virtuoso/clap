// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "camera.h"
#include "light.h"
#include "pipeline.h"
#include "pipeline-builder.h"
#include "render.h"
#include "shader_constants.h"

/****************************************************************************
 * shadow render pass operations
 ****************************************************************************/

#define DEFAULT_SHADOW_SIZE 1024
static bool shadow_resize(render_pass_ops_params *params, unsigned int *pwidth, unsigned int *pheight)
{
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

/****************************************************************************
 * pipeline builder
 ****************************************************************************/

pipeline *pipeline_build(pipeline_builder_opts *opts)
{
    if (!opts->mq)
        return NULL;

    pipeline *pl = ref_new(pipeline,
                           .width            = opts->pl_opts->width,
                           .height           = opts->pl_opts->height,
                           .light            = opts->pl_opts->light,
                           .camera           = opts->pl_opts->camera,
                           .renderer         = opts->pl_opts->renderer,
                           .render_options   = opts->pl_opts->render_options,
                           .shaders          = opts->pl_opts->shaders,
                           .name             = opts->pl_opts->name);

    struct render_pass *shadow_pass[CASCADES_MAX];

#ifdef CONFIG_GLES
    for (int i = 0; i < CASCADES_MAX; i++) {
        shadow_pass[i] =
            pipeline_add_pass(pl,
                .source               = (render_source[]){ { .mq = opts->mq, .method = RM_RENDER, }, {} },
                .ops                  = &shadow_ops,
                .multisampled         = opts->pl_opts->render_options->shadow_msaa,
                .attachment_config    = FBO_DEPTH_TEXTURE,
                .cascade              = i,
                .shader_override      = "shadow"
        );
        opts->pl_opts->light->shadow[0][i] = pipeline_pass_get_texture(shadow_pass[i]);
    }
#else
    shadow_pass[0] =
        pipeline_add_pass(pl,
            .source             = (render_source[]){ { .mq = opts->mq, .method = RM_RENDER, }, {} },
            .ops                = &shadow_ops,
            .multisampled       = opts->pl_opts->render_options->shadow_msaa,
            .attachment_config  = FBO_DEPTH_TEXTURE,
            .layers             = CASCADES_MAX,
            .cascade            = -1,
            .shader_override    = "shadow"
    );
    opts->pl_opts->light->shadow[0][0] = pipeline_pass_get_texture(shadow_pass[0]);
#endif /* CONFIG_GLES */

    texture_format hdr_fmts[] = {
#ifdef __APPLE__
        TEX_FMT_RGBA32F
#else
        TEX_FMT_RGB16F, TEX_FMT_RGBA16F, TEX_FMT_RGB32F, TEX_FMT_RGBA32F
#endif /* __APPLE__ */
    };
    texture_format hdr_fmt = TEX_FMT_RGBA8;
    for (int i = 0; i < array_size(hdr_fmts); i++)
        if (fbo_texture_supported(hdr_fmts[i])) {
            hdr_fmt = hdr_fmts[i];
            break;
        }

    struct render_pass *model_pass =
        pipeline_add_pass(pl,
            .source             = (render_source[]) {
                { .mq = opts->mq, .method = RM_RENDER, },
#ifdef CONFIG_GLES
                { .pass = shadow_pass[0], .attachment = FBO_DEPTH_TEXTURE, .method = RM_USE, .sampler = UNIFORM_SHADOW_MAP },
                { .pass = shadow_pass[1], .attachment = FBO_DEPTH_TEXTURE, .method = RM_USE, .sampler = UNIFORM_SHADOW_MAP1 },
                { .pass = shadow_pass[2], .attachment = FBO_DEPTH_TEXTURE, .method = RM_USE, .sampler = UNIFORM_SHADOW_MAP2 },
                { .pass = shadow_pass[3], .attachment = FBO_DEPTH_TEXTURE, .method = RM_USE, .sampler = UNIFORM_SHADOW_MAP3 },
                {}
#else
                { .pass = shadow_pass[0], .method = RM_USE, .sampler = UNIFORM_SHADOW_MAP }, {}
#endif /* CONFIG_GLES */
            },
            .multisampled       = true,
            .ops                = &model_ops,
            .attachment_config  = FBO_COLOR_BUFFER2,
            .name               = "model",
            .cascade            = -1,
            .color_format       = (texture_format[]) {
                                    hdr_fmt,
                                    hdr_fmt,
                                    TEX_FMT_RGBA8
                                },
            .depth_format       = TEX_FMT_DEPTH32F
    );
    struct render_pass *pass;
    pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = model_pass, .attachment = FBO_COLOR_BUFFER1, .method = RM_BLIT, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]){ hdr_fmt },
        .attachment_config  = FBO_COLOR_TEXTURE,
        .ops                = &postproc_ops,
        .shader             = "downsample",
        .scale              = 0.25,
    );

    pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = pass, .attachment = FBO_COLOR_TEXTURE, .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]){ hdr_fmt },
        .attachment_config  = FBO_COLOR_TEXTURE,
        .ops                = &postproc_ops,
        .scale              = 0.25,
        .shader             = "vblur",
    );
    pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = pass, .attachment = FBO_COLOR_TEXTURE, .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]){ hdr_fmt },
        .attachment_config  = FBO_COLOR_TEXTURE,
        .ops                = &postproc_ops,
        .scale              = 0.25,
        .shader             = "hblur",
    );
    struct render_pass *bloom_pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = pass, .attachment = FBO_COLOR_TEXTURE, .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            { .pass = model_pass, .attachment = FBO_COLOR_BUFFER1, .method = RM_BLIT, .sampler = UNIFORM_EMISSION_MAP },
            {}
        },
        .color_format       = (texture_format[]) { hdr_fmt },
        .attachment_config  = FBO_COLOR_TEXTURE,
        .ops                = &postproc_ops,
        .shader             = "upsample",
    );
    struct render_pass *sobel_pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = model_pass, .attachment = FBO_COLOR_BUFFER2, .method = RM_BLIT, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
        .attachment_config  = FBO_COLOR_TEXTURE,
        .ops                = &postproc_ops,
        .shader             = "sobel",
    );
    pass = pipeline_add_pass(pl,
        .source            = (render_source[]) {
            { .pass = model_pass, .attachment = FBO_COLOR_BUFFER0, .method = RM_BLIT, .sampler = UNIFORM_MODEL_TEX },
            { .pass = bloom_pass, .attachment = FBO_COLOR_TEXTURE, .method = RM_USE, .sampler = UNIFORM_EMISSION_MAP },
            { .pass = sobel_pass, .attachment = FBO_COLOR_TEXTURE, .method = RM_USE, .sampler = UNIFORM_SOBEL_TEX },
            {}
        },
        .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
        .ops                = &postproc_ops,
        .attachment_config  = FBO_COLOR_TEXTURE,
        .shader             = "combine",
    );
    render_pass *contrast_pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = pass, .attachment = FBO_COLOR_TEXTURE, .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
        .ops                = &postproc_ops,
        .attachment_config  = FBO_COLOR_TEXTURE,
        .shader             = "contrast",
        .checkpoint         = 1,
    );

    /* Extra blur for the menu */
    pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = pass, .attachment = FBO_COLOR_TEXTURE, .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]){ TEX_FMT_RGBA8 },
        .attachment_config  = FBO_COLOR_TEXTURE,
        .ops                = &postproc_ops,
        .shader             = "downsample",
        .scale              = 0.25,
    );
    pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = pass, .attachment = FBO_COLOR_TEXTURE, .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]){ TEX_FMT_RGBA8 },
        .attachment_config  = FBO_COLOR_TEXTURE,
        .ops                = &postproc_ops,
        .scale              = 0.25,
        .shader             = "vblur",
    );
    pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = pass, .attachment = FBO_COLOR_TEXTURE, .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            {}
        },
        .color_format       = (texture_format[]){ TEX_FMT_RGBA8 },
        .attachment_config  = FBO_COLOR_TEXTURE,
        .ops                = &postproc_ops,
        .scale              = 0.25,
        .shader             = "hblur",
    );
    pass = pipeline_add_pass(pl,
        .source             = (render_source[]) {
            { .pass = pass, .attachment = FBO_COLOR_TEXTURE, .method = RM_USE, .sampler = UNIFORM_MODEL_TEX },
            { .pass = contrast_pass, .attachment = FBO_COLOR_TEXTURE, .method = RM_USE, .sampler = UNIFORM_EMISSION_MAP },
            {}
        },
        .color_format       = (texture_format[]) { TEX_FMT_RGBA8 },
        .attachment_config  = FBO_COLOR_TEXTURE,
        .ops                = &postproc_ops,
        .shader             = "upsample",
        .checkpoint         = 2,
    );

    return pl;
}
