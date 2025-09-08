/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SHADER_H__
#define __CLAP_SHADER_H__

#include "mesh.h"
#include "shader_constants.h"
#include "object.h"
#include "render.h"

/**
 * enum shader_vars - uniforms and attributes
 * @ATTR_POSITION:                      position vertex attribute
 * @ATTR_TEX:                           texture coordinates vertex attribute
 * @ATTR_NORMAL:                        normal vector vertex attribute
 * @ATTR_TANGENT:                       tangent vector vertex attribute
 * @ATTR_JOINTS:                        influencing joints' indices vertex attribute
 * @ATTR_WEIGHTS:                       influencing joints' weights vertex attribute
 * @ATTR_MAX:                           attribute sentinel
 * @UNIFORM_MODEL_TEX:                  model texture
 * @UNIFORM_NORMAL_MAP:                 normal map texture
 * @UNIFORM_SOBEL_TEX:                  sobel texture
 * @UNIFORM_SHADOW_MAP:                 shadow map array or cascade 0 texture
 * @UNIFORM_SHADOW_MAP1:                shadow map cascade 1 texture
 * @UNIFORM_SHADOW_MAP2:                shadow map cascade 2 texture
 * @UNIFORM_SHADOW_MAP3:                shadow map cascade 3 texture
 * @UNIFORM_SHADOW_MAP_MS:              shadow multisampled array texture
 * @UNIFORM_EMISSION_MAP:               emission texture
 * @UNIFORM_LUT_TEX:                    LUT texture
 * @UNIFORM_NOISE3D_TEX:                3D noise texture
 * @UNIFORM_TEX_MAX:                    texture/sampler uniform sentinel
 * @UNIFORM_NR_TEX:                     number of texture/sampler uniforms
 * @UNIFORM_WIDTH:                      FBO width (should be useless by now)
 * @UNIFORM_HEIGHT:                     FBO height (should be useless by now)
 * @UNIFORM_NEAR_PLANE:                 view frustum's near_plane
 * @UNIFORM_FAR_PLANE:                  view frustum's far_plane
 * @UNIFORM_PROJ:                       projection matrix
 * @UNIFORM_VIEW:                       view matrix
 * @UNIFORM_TRANS:                      model TRS matrix
 * @UNIFORM_INVERSE_VIEW:               inverse view matrix
 * @UNIFORM_LIGHT_POS:                  array of light position vectors
 * @UNIFORM_LIGHT_COLOR:                array of light color vectors
 * @UNIFORM_LIGHT_DIR:                  array of light direction vectors
 * @UNIFORM_LIGHT_DIRECTIONAL:          array of "is light directional" booleans
 * @UNIFORM_NR_LIGHTS:                  number of light sources
 * @UNIFORM_LIGHT_AMBIENT:              ambient light color
 * @UNIFORM_ATTENUATION:                array of non-directional light attenuations
 * @UNIFORM_SHINE_DAMPER:               specular shine damper for Blinn-Phong lighting
 * @UNIFORM_REFLECTIVITY:               reflectivity for Blinn-Phong lighting
 * @UNIFORM_ROUGHNESS:                  roughness for Cook-Torrance lighting
 * @UNIFORM_METALLIC:                   metallic far Cook-Torrance lighting
 * @UNIFORM_ROUGHNESS_CEIL:             roughness ceiling for procedural roughness
 * @UNIFORM_ROUGHNESS_AMP:              roughness per-octave amplitude multiplier
 * @UNIFORM_ROUGHNESS_OCT:              number of octaves for procedural roughness
 * @UNIFORM_ROUGHNESS_SCALE:            fragment coords scale for seeding
 * @UNIFORM_METALLIC_CEIL:              metallic ceiling for procedural metallic
 * @UNIFORM_METALLIC_AMP:               metallic per-octave amplitude multiplier
 * @UNIFORM_METALLIC_OCT:               number of octaves for procedural metallic
 * @UNIFORM_METALLIC_SCALE:             fragment coords scale for seeding
 * @UNIFORM_METALLIC_MODE:              0: metallic=roughness, 1: metallic=1-roughness,
 *                                      2: independent
 * @UNIFORM_SHARED_SCALE:               boolean: metallic noise seed is roughness seed
 * @UNIFORM_USE_NOISE_NORMALS:          boolean: generate per-fragment normals in the shader
 * @UNIFORM_IN_COLOR:                   override color
 * @UNIFORM_COLOR_PASSTHROUGH:          COLOR_PT_NONE: no override;
 *                                      COLOR_PT_ALPHA: override alpha;
 *                                      COLOR_PT_ALL: override all color components
 * @UNIFORM_SHADOW_VSM:                 use Variance Shadow Mapping (otherwise CSM)
 * @UNIFORM_SHADOW_MVP:                 array of proj * view matrices for shadow cascades
 * @UNIFORM_CASCADE_DISTANCES:          array of cascade distances from the camera
 * @UNIFORM_SHADOW_TINT:                color of the shadow tint
 * @UNIFORM_SHADOW_OUTLINE:             boolean: outline edges of shadows
 * @UNIFORM_SHADOW_OUTLINE_THRESHOLD:   shadow edge outline cutoff threshold
 * @UNIFORM_OUTLINE_EXCLUDE:            boolean: exclude model from edge detection
 * @UNIFORM_LAPLACE_KERNEL:             Laplace kernel size: 3x3 or 5x5
 * @UNIFORM_SOBEL_SOLID_ID:             unique value for solid-color outlines
 * @UNIFORM_USE_NORMALS:                model uses a normal map
 * @UNIFORM_USE_SKINNING:               model uses skeletal animotion (joint, weights, joint_transform)
 * @UNIFORM_USE_MSAA:                   [ropt] use multisampled textures
 * @UNIFORM_USE_HDR:                    [ropt] use half-float or float components for colors
 *                                      in intermediate postprocessing render passes
 * @UNIFORM_USE_SSAO:                   [ropt] use screen space ambient occlusion
 * @UNIFORM_SSAO_KERNEL:                [ropt] SSAO kernel size
 * @UNIFORM_SSAO_NOISE_SCALE:           [ropt] UV scale for SSAO noise sampling
 * @UNIFORM_SSAO_RADIUS:                [ropt] SSAO radius
 * @UNIFORM_SSAO_WEIGHT:                [ropt] SSAO influence
 * @UNIFORM_SOBEL_SOLID:                boolean: use diffuse colors for edge detection
 *                                      instead of normal vectors
 * @UNIFORM_JOINT_TRANSFORMS:           array of joint transform TRS matrices
 * @UNIFORM_BLOOM_EXPOSURE:             [ropt] bloom exposure
 * @UNIFORM_BLOOM_INTENSITY:            [ropt] >0: emission map's bloom intensity;
 *                                      <0: diffuse color's bloom intensity
 * @UNIFORM_BLOOM_THRESHOLD:            [ropt] emission cutoff
 * @UNIFORM_BLOOM_OPERATOR:             [ropt] HDR bloom tonemapping operator
 * @UNIFORM_LIGHTING_EXPOSURE:          [ropt] lighting exposure
 * @UNIFORM_LIGHTING_OPERATOR:          [ropt] HDR lighting tonemapping operator
 * @UNIFORM_CONTRAST:                   [ropt] contrast
 * @UNIFORM_FOG_NEAR:                   [ropt] radial fog's near distance
 * @UNIFORM_FOG_FAR:                    [ropt] radial fog's far distance
 * @UNIFORM_FOG_COLOR:                  [ropt] radial fog color
 * @UNIFORM_PARTICLE_POS:               array of particle position vertices
 * @SHADER_VAR_MAX:                     sentinel
 */
enum shader_vars {
    ATTR_POSITION   = ATTR_LOC_POSITION,
    ATTR_TEX        = ATTR_LOC_TEX,
    ATTR_NORMAL     = ATTR_LOC_NORMAL,
    ATTR_TANGENT    = ATTR_LOC_TANGENT,
    ATTR_JOINTS     = ATTR_LOC_JOINTS,
    ATTR_WEIGHTS    = ATTR_LOC_WEIGHTS,
    ATTR_MAX,
    UNIFORM_MODEL_TEX = ATTR_MAX,
    UNIFORM_NORMAL_MAP,
    UNIFORM_SOBEL_TEX,
    UNIFORM_SHADOW_MAP,
    UNIFORM_SHADOW_MAP1,
    UNIFORM_SHADOW_MAP2,
    UNIFORM_SHADOW_MAP3,
    UNIFORM_SHADOW_MAP_MS,
    UNIFORM_EMISSION_MAP,
    UNIFORM_LUT_TEX,
    UNIFORM_NOISE3D_TEX,
    UNIFORM_LIGHT_MAP,
    UNIFORM_TEX_MAX,
    UNIFORM_NR_TEX = UNIFORM_TEX_MAX - ATTR_MAX,
    UNIFORM_WIDTH = UNIFORM_TEX_MAX,
    UNIFORM_HEIGHT,
    UNIFORM_NEAR_PLANE,
    UNIFORM_FAR_PLANE,
    UNIFORM_PROJ,
    UNIFORM_VIEW,
    UNIFORM_TRANS,
    UNIFORM_INVERSE_VIEW,
    UNIFORM_LIGHT_POS,
    UNIFORM_LIGHT_COLOR,
    UNIFORM_LIGHT_DIR,
    UNIFORM_LIGHT_DIRECTIONAL,
    UNIFORM_LIGHT_CUTOFF,
    UNIFORM_NR_LIGHTS,
    UNIFORM_LIGHT_AMBIENT,
    UNIFORM_ATTENUATION,
    UNIFORM_SHINE_DAMPER,
    UNIFORM_REFLECTIVITY,
    UNIFORM_ROUGHNESS,
    UNIFORM_METALLIC,
    UNIFORM_ROUGHNESS_CEIL,
    UNIFORM_ROUGHNESS_AMP,
    UNIFORM_ROUGHNESS_OCT,
    UNIFORM_ROUGHNESS_SCALE,
    UNIFORM_METALLIC_CEIL,
    UNIFORM_METALLIC_AMP,
    UNIFORM_METALLIC_OCT,
    UNIFORM_METALLIC_SCALE,
    UNIFORM_METALLIC_MODE,
    UNIFORM_SHARED_SCALE,
    UNIFORM_USE_NOISE_NORMALS,
    UNIFORM_NOISE_NORMALS_AMP,
    UNIFORM_NOISE_NORMALS_SCALE,
    UNIFORM_USE_NOISE_EMISSION,
    UNIFORM_USE_3D_FOG,
    UNIFORM_FOG_3D_AMP,
    UNIFORM_FOG_3D_SCALE,
    UNIFORM_IN_COLOR,
    UNIFORM_COLOR_PASSTHROUGH,
    UNIFORM_SHADOW_VSM,
    UNIFORM_SHADOW_MVP,
    UNIFORM_CASCADE_DISTANCES,
    UNIFORM_SHADOW_TINT,
    UNIFORM_SHADOW_OUTLINE,
    UNIFORM_SHADOW_OUTLINE_THRESHOLD,
    UNIFORM_NR_CASCADES,
    UNIFORM_OUTLINE_EXCLUDE,
    UNIFORM_LAPLACE_KERNEL,
    UNIFORM_SOBEL_SOLID_ID,
    UNIFORM_USE_NORMALS,
    UNIFORM_USE_SKINNING,
    UNIFORM_USE_MSAA,
    UNIFORM_USE_HDR,
    UNIFORM_USE_SSAO,
    UNIFORM_SSAO_KERNEL,
    UNIFORM_SSAO_NOISE_SCALE,
    UNIFORM_SSAO_RADIUS,
    UNIFORM_SSAO_WEIGHT,
    UNIFORM_SOBEL_SOLID,
    UNIFORM_JOINT_TRANSFORMS,
    UNIFORM_BLOOM_EXPOSURE,
    UNIFORM_BLOOM_INTENSITY,
    UNIFORM_BLOOM_THRESHOLD,
    UNIFORM_BLOOM_OPERATOR,
    UNIFORM_LIGHTING_EXPOSURE,
    UNIFORM_LIGHTING_OPERATOR,
    UNIFORM_CONTRAST,
    UNIFORM_FOG_NEAR,
    UNIFORM_FOG_FAR,
    UNIFORM_FOG_COLOR,
    UNIFORM_PARTICLE_POS,
    SHADER_VAR_MAX
};

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
 * @p:  shader program
 *
 * Take a shader program into use. Needs a matching shader_prog_done().
 * Context: rendering, resource loading
 */
void shader_prog_use(struct shader_prog *p);

/**
 * shader_prog_done() - unbind a shader program
 * @p:  shader program
 *
 * Stop using a shader program. Matches a preceding shader_prog_done().
 * Context: rendering, resource loading
 */
void shader_prog_done(struct shader_prog *p);

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

must_check cresp(shader_context) shader_vars_init(void);
void shader_vars_done(shader_context *ctx);
void shader_var_blocks_update(struct shader_prog *p);

#endif /* __CLAP_SHADER_H__ */
