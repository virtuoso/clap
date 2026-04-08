/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SHADER_H__
#define __CLAP_SHADER_H__

#include "mesh.h"
#include "shader_constants.h"
#include "object.h"
#include "render.h"
#include "bindings/render-bindings.h"

typedef struct shader_context shader_context;
cresp_ret(shader_context);

struct shader_prog;

DEFINE_REFCLASS_INIT_OPTIONS(shader_prog,
    shader_context  *ctx;
    const char      *name;
    const char      *vert_text;
    const char      *geom_text;
    const char      *frag_text;
    const char      *vert_ref_text;
    const char      *geom_ref_text;
    const char      *frag_ref_text;
);
DECLARE_REFCLASS(shader_prog);

/**
 * shader_name() - get shader name string
 * @p:  shader program
 * Return: shader program's name
 */
const char *shader_name(struct shader_prog *p);

/**
 * shader_prog_use() - bind a shader program
 * @p:      shader program
 * @draw:   use shader for drawing
 *
 * Take a shader program into use. Needs a matching shader_prog_done().
 * Context: rendering [@draw==%true], resource loading [@draw==%false]
 * Return: error from the rendering backend or CERR_OK
 */
cerr_check shader_prog_use(struct shader_prog *p, bool draw);

/**
 * shader_prog_done() - unbind a shader program
 * @p:      shader program
 * @draw:   used shader for drawing
 *
 * Stop using a shader program. Matches a preceding shader_prog_done().
 * Context: rendering [@draw==%true], resource loading [@draw==%false]
 */
void shader_prog_done(struct shader_prog *p, bool draw);

/**
 * shader_prog_renderer() - get shader's renderer
 * @p:  shader program
 *
 * Return: pointer to renderer_t object, non-NULL
 */
renderer_t *shader_prog_renderer(struct shader_prog *p);

/**
 * shader_prog_shader() - get shader's low-level shader object
 * @p:  shader program
 *
 * Return: pointer to shader_t object, non-NULL
 */
shader_t *shader_prog_shader(struct shader_prog *p);

/**
 * shader_prog_renderer() - get shader's renderer
 * @p:  shader program
 *
 * Return: pointer to renderer_t object, non-NULL
 */
renderer_t *shader_prog_renderer(struct shader_prog *p);

/**
 * shader_get_var_name() - get a shader variable name string
 * @var:    shader variable (attribute/uniform)
 * Context: anywhere
 * Return: name string
 */
const char *shader_get_var_name(enum shader_vars var);

/**
 * shader_has_var() - check if shader program uses a variable
 * @p:      shader program
 * @var:    shader variable (attribute/uniform)
 *
 * Return:
 * * true: it does
 * * false: it doesn't
 */
bool shader_has_var(struct shader_prog *p, enum shader_vars var);

/**
 * shader_set_var_ptr() - set/fill an array uniform
 * @p:      shader program
 * @var:    uniform shader variable
 * @count:  number of elements
 * @value:  data
 *
 * The @data is treated as an array of uniform's data type (shader_var_desc[var].type),
 * @count is the number of elements of that type.
 * Context: shader program must be in use for standalone non-opaque uniforms (which don't
 * exist any more), otherwise virtually anywhere.
 */
void shader_set_var_ptr(struct shader_prog *p, enum shader_vars var,
                        unsigned int count, void *value);

/**
 * shader_set_var_float() - set a float uniform
 * @p:      shader program
 * @var:    uniform shader variable
 * @value:  target value
 *
 * Set a value for a single float uniform, same as `shader_set_var_ptr(p, var, 1, &value);`
 * Context: same as shader_set_var_ptr()
 */
void shader_set_var_float(struct shader_prog *p, enum shader_vars var, float value);

/**
 * shader_set_var_int() - set an integer uniform
 * @p:      shader program
 * @var:    uniform shader variable
 * @value:  target value
 *
 * Set a value for a single integer uniform, same as `shader_set_var_ptr(p, var, 1, &value);`
 * Context: same as shader_set_var_ptr()
 */
void shader_set_var_int(struct shader_prog *p, enum shader_vars var, int value);

/**
 * shader_setup_attributes() - set up multiple attribute buffers for a mesh
 * @p:      shader program
 * @buf:    array of ATTR_MAX butter_t buffers to be configured and loaded
 * @mesh:   mesh with vertex attributes from which to load the buffers
 *
 * Load multiple vertex attributes from a mesh into a contiguous buffer (main)
 * and set up the rest of the buffers with offsets and sizes and a link to the
 * main buffer, so they can be bound all at once to a single binding point.
 *
 * Return: CERR_OK on success or a error code otherwise.
 */
cerr shader_setup_attributes(struct shader_prog *p, buffer_t *buf, struct mesh *mesh);

/**
 * shader_plug_attributes() - plug vertex attributes from a buffer array
 * @p:      shader program
 * @buf:    array of ATTR_MAX buffer_t buffers to be bound
 *
 * Bind vertex attribute buffers to a shader program before drawing.
 */
void shader_plug_attributes(struct shader_prog *p, buffer_t *buf);

/**
 * shader_unplug_attributes() - unplug vertex attributes from a buffer array
 * @p:      shader program
 * @buf:    array of ATTR_MAX buffer_t buffers to be bound
 *
 * Unbind vertex attribute buffers after drawing.
 */
void shader_unplug_attributes(struct shader_prog *p, buffer_t *buf);

/**
 * shader_get_texture_slot() - get assigned texture binding slot
 * @p:      shader program
 * @var:    texture uniform shader variable
 *
 * Obtain an assigned texture slot (shader_var_desc[var].texture_slot) for a
 * texture uniform @var, if the shader program recognizes it
 * Context: anywhere
 * Return:
 * * >= 0: texture slot
 * * -1: if shader doesn't use this texture or if @var is not a texture uniform
 */
int shader_get_texture_slot(struct shader_prog *p, enum shader_vars var);
void shader_plug_texture(struct shader_prog *p, enum shader_vars var, texture_t *tex);
void shader_plug_textures_multisample(struct shader_prog *p, bool multisample,
                                      enum shader_vars tex_var, enum shader_vars ms_var,
                                      texture_t *ms_tex);
void shader_unplug_texture(struct shader_prog *p, enum shader_vars var, texture_t *tex);
struct shader_prog *shader_prog_find(struct list *shaders, const char *name);
void shaders_free(struct list *shaders);
cerr lib_request_shaders(shader_context *ctx, const char *name, struct list *shaders);
cresp(shader_prog) shader_prog_find_get(shader_context *ctx, struct list *shaders, const char *name);

must_check cresp(shader_context) shader_vars_init(renderer_t *renderer);
void shader_vars_done(shader_context *ctx);
void shader_var_blocks_update(struct shader_prog *p);

#endif /* __CLAP_SHADER_H__ */
