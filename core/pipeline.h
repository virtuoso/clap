/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PIPELINE_H__
#define __CLAP_PIPELINE_H__

#include "camera.h"
#include "clap.h"
#include "lut.h"
#include "shader.h"

typedef struct render_pass render_pass;
cresp_ret(render_pass);

typedef struct pipeline pipeline;

typedef struct render_options {
    lut     *lighting_lut;
    float   bloom_exposure;
    float   bloom_intensity;
    float   bloom_threshold;
    float   bloom_operator;
    float   lighting_exposure;
    float   lighting_operator;
    float   contrast;
    float   ssao_radius;
    float   ssao_weight;
    float   fog_near;
    float   fog_far;
    vec3    fog_color;
    bool    shadow_outline;
    bool    shadow_msaa;
    bool    model_msaa;
    bool    debug_draws_enabled;
    float   shadow_outline_threshold;
    int     laplace_kernel;
    bool    edge_antialiasing;
    bool    edge_sobel;
    bool    ssao;
    bool    shadow_vsm;
    bool    hdr;
    bool    collision_draws_enabled;
    bool    aabb_draws_enabled;
    bool    camera_frusta_draws_enabled;
    bool    light_frusta_draws_enabled;
    bool    overlay_draws_enabled;
} render_options;

typedef enum {
    /* blit from source's fbo attachment into blit_fbo */
    RM_BLIT,
    /*
     * use source's fbo attachment directly
     * render_source::attachment must be FBO_COLOR_TEXTURE
     */
    RM_USE,
    /* render a model queue render_source::mq info fbo */
    RM_RENDER,
    /* use a texture */
    RM_PLUG,
} render_method;

typedef struct render_source {
    union {
        render_pass     *pass;
        texture_t       *tex;
        struct mq       *mq;
    };
    fbo_attachment      attachment;
    render_method       method;
    enum shader_vars    sampler;
} render_source;

typedef struct render_pass_ops_params {
    renderer_t      *renderer;
    struct light    *light;
    struct camera   *camera;
    float           render_scale;
    float           near_plane;
    float           far_plane;
} render_pass_ops_params;

#define RENDER_PASS_OPS_PARAMS(_pl, _pass) \
    render_pass_ops_params params = { \
        .renderer       = (_pl)->renderer, \
        .camera         = (_pl)->camera, \
        .light          = (_pl)->light, \
        .near_plane     = (_pl)->camera->view.main.near_plane, \
        .far_plane      = (_pl)->camera->view.main.far_plane, \
        .render_scale   = (_pass)->scale, \
    };

typedef struct render_pass_ops {
    bool    (*resize)(render_pass_ops_params *params, unsigned int *pwidth, unsigned int *pheight);
    void    (*prepare)(render_pass_ops_params *params);
} render_pass_ops;

typedef struct pipeline_pass_config {
    /*
     * Array of sources from which to render this pass;
     * terminated with an empty source {}
     */
    render_source           *source;
    /*
     * Callbacks for setting up fbo, resizing and preparing to render;
     * not optional
     */
    const render_pass_ops   *ops;
    /* Shader with which to draw render_pass::quad */
    const char              *shader;
    /* Shader that overrides the shaders of models on render_source::mq */
    const char              *shader_override;
    const char              *name;
    /*
     * Array of color formats, one for each of the FBO attachments specified by
     * render_pass::attachment_config
     */
    texture_format          *color_format;
    /* Color format for the depth buffer */
    texture_format          depth_format;
    /* Make FBO attachment texture an array of [layers] textures */
    unsigned int            layers;
    /* Determines the number and types of attachments of the pass' FBO */
    fbo_attachment          attachment_config;
    unsigned int            checkpoint;
    float                   scale;
    int                     cascade;
    bool                    multisampled;
} pipeline_pass_config;

DEFINE_REFCLASS_INIT_OPTIONS(pipeline,
    const char      *name;
    clap_context    *clap_ctx;
    struct light    *light;
    struct camera   *camera;
    ssao_state      *ssao_state;
    unsigned int    width;
    unsigned int    height;
);
DECLARE_REFCLASS(pipeline);

void pipeline_clearout(pipeline *pl);
void pipeline_resize(struct pipeline *pl, unsigned int width, unsigned int height);
cresp(shader_prog) pipeline_shader_find_get(pipeline *pl, const char *name);
cresp(render_pass) _pipeline_add_pass(struct pipeline *pl, const pipeline_pass_config *cfg);
#define pipeline_add_pass(_pl, args...) \
    _pipeline_add_pass((_pl), &(pipeline_pass_config){ args })
cresp(render_pass) pipeline_find_pass(pipeline *pl, const char *name);
void render_pass_plug_texture(render_pass *pass, enum shader_vars sampler, texture_t *tex);
void pipeline_render(struct pipeline *pl, unsigned int checkpoint);
texture_t *pipeline_pass_get_texture(struct render_pass *pass, fbo_attachment attachment);
float pipeline_pass_get_scale(render_pass *pass);
#ifndef CONFIG_FINAL
void pipeline_debug(struct pipeline *pl);
#else
static inline void pipeline_debug(struct pipeline *pl) {}
#endif /* CONFIG_FINAL */

#endif /* __CLAP_PIPELINE_H__ */
