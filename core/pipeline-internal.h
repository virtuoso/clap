/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PIPELINE_INTERNAL_H__
#define __CLAP_PIPELINE_INTERNAL_H__

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
    int                 width;
    int                 height;

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
