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

/*
 * bloom_quality levels, the player-facing knob over the bloom setup:
 * see graphics_settings_set_bloom_quality()
 */
typedef enum bloom_quality {
    /* one quarter-res blur chain, no distance scaling: the fewest fetches */
    BLOOM_QUALITY_LOW = 0,
    /* two blur chains, halos scaled with distance over a 2:1 range */
    BLOOM_QUALITY_MEDIUM,
    /* mip pyramid, halos scaled with distance over a 10:1 range */
    BLOOM_QUALITY_HIGH,
    BLOOM_QUALITY_MAX,
} bloom_quality;

/* The ambient occlusion pass shader; render_options.ssao switches the pass itself */
typedef enum ao_method {
    /* hemisphere kernel, SSAO_KERNEL_SIZE depth taps per fragment: ssao.frag */
    AO_METHOD_SSAO = 0,
    /* horizon marching, GTAO_SLICES x 2 x GTAO_STEPS depth taps per fragment: gtao.frag */
    AO_METHOD_GTAO,
    AO_METHOD_MAX,
} ao_method;

typedef struct render_options {
    lut     *lighting_lut;
    float   bloom_exposure;
    float   bloom_intensity;
    float   bloom_threshold;
    float   bloom_operator;
    float   bloom_distance;
    float   lighting_exposure;
    float   lighting_operator;
    float   contrast;
    float   ssao_radius;
    float   ssao_weight;
    /* per-frame shader state: seconds and frames since app start */
    float   uptime;
    unsigned int    frame_count;
    float   film_grain_factor;
    float   film_grain_power;
    /* TAA: current frame's contribution to the history blend */
    float   taa_feedback;
    float   fog_near;
    float   fog_far;
    /* the fog factor at and past fog_far */
    float   fog_max;
    vec3    fog_color;
    float   volumetric_intensity;
    float   volumetric_g;
    float   volumetric_density;
    float   volumetric_amp;
    float   volumetric_scale;
    float   volumetric_local_intensity;
    float   hdr_white_nits;
    float   hdr_peak_nits;
    float   hdr_compress_knee;
    float   hdr_knee_softness;
    /*
     * HDR output encoding, one of HDR_TRANSFER_* (shader_constants.h).
     * @hdr_transfer_want is the request, settable at runtime; @hdr_transfer
     * is what the backend's surface actually came back with and what
     * present.frag encodes for, refreshed every frame in clap_frame().
     * They differ when the compositor doesn't offer the requested pair.
     */
    int     hdr_transfer;
    int     hdr_transfer_want;
    /*
     * Model pass resolution scale (0..1]: render the model pass at a
     * fraction of the pipeline size, postprocessing samples it back up.
     * The frame is fragment-bound on small GPUs, so the model pass cost
     * scales almost linearly with this
     */
    float   render_scale;
    bool    shadow_outline;
    bool    shadow_msaa;
    bool    model_msaa;
    bool    debug_draws_enabled;
    float   shadow_outline_threshold;
    /*
     * Shadow casters smaller than this many texels of the target
     * cascade's map are skipped in its shadow pass; 0 disables the cull
     */
    float   shadow_caster_min_size;
    int     laplace_kernel;
    bool    edge_antialiasing;
    bool    edge_sobel;
    bool    ssao;
    /* volumetric scattering pass; fog_far bounds the march */
    bool    fog_volumetric;
    bool    fog_volumetric_debug;
    bool    shadow_vsm;
    /* VSM light bleed reduction: the Chebyshev tail written off as bleed */
    float   shadow_bleed;
    /* one of BLOOM_QUALITY_*, picks the bloom passes the pipeline builder emits */
    int     bloom_quality;
    /* one of AO_METHOD_*, picks the AO pass shader when ssao is on */
    int     ao_method;
    /* one of FOG_CURVE_* (shader_constants.h) */
    int     fog_curve;
    /*
     * The edge (outline) pass; when off, the pass is not built at all and
     * combine/present composite without edges (SMAA goes down with it)
     */
    bool    edges;
    bool    film_grain;
    bool    fog;
    bool    taa;
    bool    hdr;
    bool    hdr_output;
    bool    hdr_output_enabled;
    bool    collision_draws_enabled;
    bool    velocity_draws_enabled;
    bool    aabb_draws_enabled;
    bool    degenerate_draws_enabled;
    bool    camera_frusta_draws_enabled;
    bool    light_frusta_draws_enabled;
    bool    overlay_draws_enabled;
    bool    light_draws_enabled;
} render_options;

/**
 * ropts_shader_variants() - the global half of the shader variant mask
 * @ropts:  render options
 *
 * The SHADER_*_BIT axes every model shader needs under @ropts, minimal:
 * axes whose paths can't be exercised with these options stay off, which
 * in variant builds is exactly the compiled specialization. TAA follows
 * CONFIG_TAA alone: the model pass binds the velocity RT whenever it's
 * compiled in (see pipeline-builder.c), runtime taa only zeroes it.
 * OR with model3dtx_shader_variants() for the full mask.
 * Context: anywhere
 * Return: the variant bits
 */
shader_variants_t ropts_shader_variants(const render_options *ropts);

/**
 * struct taa_params - per-frame TAA state
 * @proj_jittered:      camera projection with this frame's sub-pixel jitter
 * @inv_proj_jittered:  its inverse, for depth-based position reconstruction
 * @prev_proj_view:     proj * view (unjittered) of the previous frame
 * @jitter:             this frame's sub-pixel jitter in NDC units
 * @reset:              the history buffers hold garbage: the resolve outputs the
 *                      current frame alone
 *
 * Built by the pipeline for camera-driven passes; models_render() uploads it
 * when set.
 */
typedef struct taa_params {
    mat4x4  proj_jittered;
    mat4x4  inv_proj_jittered;
    mat4x4  prev_proj_view;
    vec2    jitter;
    bool    reset;
} taa_params;

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
    /*
     * sample this pass' own previous-frame output; requires
     * pipeline_pass_config::ping_pong on the same pass
     */
    RM_HISTORY,
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
    int             cascade;
} render_pass_ops_params;

#define RENDER_PASS_OPS_PARAMS(_pl, _pass) \
    render_pass_ops_params params = { \
        .renderer       = (_pl)->renderer, \
        .camera         = (_pl)->camera, \
        .light          = (_pl)->light, \
        .near_plane     = (_pl)->camera->view.main.near_plane, \
        .far_plane      = (_pl)->camera->view.main.far_plane, \
        .render_scale   = (_pass)->scale, \
        .cascade        = (_pass)->cascade, \
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
     * Array of attachment configs, one for each of the FBO attachments
     * specified by render_pass::layout
     */
    fbo_attconfig           *color_config;
    /* Depth attachment config */
    fbo_attconfig           depth_config;
    /* Make FBO attachment texture an array of [layers] textures */
    unsigned int            layers;
    /* Determines the number and types of attachments of the pass' FBO */
    fbo_attachment          layout;
    unsigned int            checkpoint;
    float                   scale;
    int                     cascade;
    bool                    multisampled;
    /*
     * Allocate two FBOs and swap them each frame; the previous frame's
     * render target is sampled via an RM_HISTORY source. A ping-pong
     * pass must render every frame, i.e. it must sit before the lowest
     * checkpoint and never render to the screen.
     */
    bool                    ping_pong;
} pipeline_pass_config;

DEFINE_REFCLASS_INIT_OPTIONS(pipeline,
    const char      *name;
    clap_context    *clap_ctx;
    struct light    *light;
    struct camera   *camera;
    ssao_state      *ssao_state;
    texture_t       *noise3d;
    unsigned int    nr_cascades;
);
DECLARE_REFCLASS(pipeline);
DECLARE_CLEANUP(pipeline);

void pipeline_clearout(pipeline *pl);
void pipeline_resize(struct pipeline *pl, unsigned int width, unsigned int height);
cresp(shader_prog) pipeline_shader_find_get(pipeline *pl, const char *name);

/**
 * pipeline_render_options() - get the pipeline's render options
 * @pl: the pipeline
 * Return: the render options the pipeline was built with, non-NULL
 */
render_options *pipeline_render_options(pipeline *pl);

/**
 * pipeline_shader_variant_get() - get the shader covering a variant set
 * @pl:         the pipeline
 * @base:       uber shader family base name
 * @variants:   the full variant mask (per-model | global halves)
 *
 * The shader compiled with exactly @variants when one exists (variant
 * builds), the uber @base otherwise: the uber branches on uniforms, so
 * it covers any mask. Takes a reference either way.
 * Return: the shader program or an error from shader/pipeline loading
 */
cresp(shader_prog) pipeline_shader_variant_get(pipeline *pl, const char *base,
                                               shader_variants_t variants);

struct mq;
typedef struct model3d model3d;

/**
 * pipeline_model_shadow_resolve() - resolve a model's shadow-pass shader
 * @pl:     the pipeline
 * @model:  the model, variant managed (model3d::shader_base set)
 *
 * Give @model the shadow-pass counterpart of its model prog: same
 * attribute-layout bits, so both read the model's vertex buffer the same
 * way, in the family the shadow passes use for the current options.
 * A no-op for models that aren't variant managed.
 * Return: CERR_OK or a shader loading error
 */
cerr pipeline_model_shadow_resolve(pipeline *pl, model3d *model);

/**
 * pipeline_shaders_resolve() - re-resolve variant-managed model shaders
 * @pl: the pipeline
 * @mq: the model queue
 *
 * For every txmodel whose model3d opted in (shader_base set) and whose
 * composed variant mask changed since its last resolution, resolve the
 * mask against the compiled set and swap the prog if it resolves to a
 * different one. The attribute-layout bits are frozen at model
 * creation, so swaps never change the vertex layout. Call after a
 * pipeline rebuild, where the global mask half follows render options,
 * and when pipeline_shaders_are_stale() says a material changed the
 * other half. A mask that fails to resolve keeps the model's old prog
 * until the mask changes again.
 * Return: CERR_OK or the first resolution error
 */
cerr pipeline_shaders_resolve(pipeline *pl, struct mq *mq);

/**
 * pipeline_shaders_are_stale() - tell if a model drew with a stale shader variant
 * @pl: the pipeline
 *
 * The frag-only axes of a variant-managed model's mask follow its
 * material, which can change after the prog was resolved: a model built
 * before its material was filled in, a debug UI edit. The camera passes
 * flag such a model as they draw it; pipeline_shaders_resolve() clears
 * the flag.
 * Return: true if pipeline_shaders_resolve() has a prog to swap
 */
bool pipeline_shaders_are_stale(pipeline *pl);
cresp(render_pass) _pipeline_add_pass(struct pipeline *pl, const pipeline_pass_config *cfg);
#define pipeline_add_pass(_pl, args...) \
    _pipeline_add_pass((_pl), &(pipeline_pass_config){ args })
cresp(render_pass) pipeline_find_pass(pipeline *pl, const char *name);
void render_pass_plug_texture(render_pass *pass, enum shader_vars sampler, texture_t *tex);
cerr_check pipeline_render(struct pipeline *pl, unsigned int checkpoint);
texture_t *pipeline_pass_get_texture(struct render_pass *pass, fbo_attachment attachment);
float pipeline_pass_get_scale(render_pass *pass);
#ifndef CONFIG_FINAL
void pipeline_debug(struct pipeline *pl);
#else
static inline void pipeline_debug(struct pipeline *pl) {}
#endif /* CONFIG_FINAL */

#endif /* __CLAP_PIPELINE_H__ */
