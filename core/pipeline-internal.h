/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PIPELINE_INTERNAL_H__
#define __CLAP_PIPELINE_INTERNAL_H__

#include "pipeline.h"
#include "pipeline-debug.h"
#include "render.h"
#include "ssao.h"

struct pipeline {
    struct ref          ref;
    struct list         passes;
    const char          *name;
    renderer_t          *renderer;
    render_options      *render_options;
    struct list         shaders;
    shader_context      *shader_ctx;
    struct camera       *camera;
    struct light        *light;
    ssao_state          *ssao_state;
    unsigned int        nr_cascades;
    int                 width;
    int                 height;
    /*
     * TAA per-frame state fed to models_render() for camera passes;
     * taa::prev_proj_view is captured at the end of pipeline_render(),
     * the jitters advance at the top of it
     */
    taa_params          taa;
    /* Halton sequence position */
    unsigned int        jitter_seq;
    /* render_options::taa, sampled once per frame in pipeline_render() */
    bool                taa_active;
    /*
     * False while ping-pong back buffers hold garbage (fresh build,
     * clearout or resize); set by pipeline_render() once a frame has
     * been rendered: whoever samples an RM_HISTORY source checks it
     * before trusting the contents
     */
    bool                history_valid;

    PIPELINE_DEBUG_DATA;
};

struct render_pass {
    /* Array [nr_sources] of render_source objects */
    render_source           *source;
    /*
     * Blit from sources into this fbo array [nr_sources]:
     * blit_fbo[x] only exists if source x needs blitting
     */
    fbo_t                   **blit_fbo;
    texture_t               **use_tex;
    /* Render output: always exists */
    fbo_t                   *fbo;
    /*
     * Ping-pong only (NULL otherwise): previous frame's render target,
     * swapped with fbo at the top of pass_resolve_sources(), sampled
     * by this pass' RM_HISTORY source
     */
    fbo_t                   *fbo_back;
    /*
     * Postprocessing passes assemble textures from sources' fbos
     * and this pass' blit_fbos into this quad and render it into
     * render_pass::fbo
     */
    entity3d                *quad;
    /*
     * Callbacks for setting up fbo, resizing and preparing to render;
     * not optional
     */
    const render_pass_ops   *ops;
    /* Link to pipeline::passes list */
    struct list             entry;
    /* Shader to override render_source::mq's models' shaders */
    struct shader_prog      *prog_override;
    const char              *name;
    unsigned int            nr_sources;
    int                     cascade;
    /* Scale down (or up) fbo's width and height by this much on resize */
    float                   scale;

    unsigned int            checkpoint;
};

#endif /* __CLAP_PIPELINE_INTERNAL_H__ */
