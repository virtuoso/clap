/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SHADER_H__
#define __CLAP_SHADER_H__

#include "object.h"
#include "render.h"

enum shader_vars {
    ATTR_POSITION = 0,
    ATTR_NORMAL,
    ATTR_TEX,
    ATTR_TANGENT,
    ATTR_JOINTS,
    ATTR_WEIGHTS,
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
    UNIFORM_WIDTH,
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
    UNIFORM_ATTENUATION,
    UNIFORM_SHINE_DAMPER,
    UNIFORM_REFLECTIVITY,
    UNIFORM_HIGHLIGHT_COLOR,
    UNIFORM_IN_COLOR,
    UNIFORM_COLOR_PASSTHROUGH,
    UNIFORM_SHADOW_MVP,
    UNIFORM_CASCADE_DISTANCES,
    UNIFORM_SHADOW_OUTLINE,
    UNIFORM_SHADOW_OUTLINE_THRESHOLD,
    UNIFORM_OUTLINE_EXCLUDE,
    UNIFORM_LAPLACE_KERNEL,
    UNIFORM_SOBEL_SOLID_ID,
    UNIFORM_USE_NORMALS,
    UNIFORM_USE_SKINNING,
    UNIFORM_USE_MSAA,
    UNIFORM_USE_HDR,
    UNIFORM_SOBEL_SOLID,
    UNIFORM_JOINT_TRANSFORMS,
    UNIFORM_BLOOM_EXPOSURE,
    UNIFORM_BLOOM_INTENSITY,
    UNIFORM_BLOOM_THRESHOLD,
    UNIFORM_BLOOM_OPERATOR,
    UNIFORM_LIGHTING_EXPOSURE,
    UNIFORM_LIGHTING_OPERATOR,
    UNIFORM_CONTRAST,
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
);
DECLARE_REFCLASS(shader_prog);

const char *shader_name(struct shader_prog *p);
void shader_prog_use(struct shader_prog *p);
void shader_prog_done(struct shader_prog *p);
const char *shader_get_var_name(enum shader_vars var);
bool shader_has_var(struct shader_prog *p, enum shader_vars var);
void shader_set_var_ptr(struct shader_prog *p, enum shader_vars var,
                        unsigned int count, void *value);
void shader_set_var_float(struct shader_prog *p, enum shader_vars var, float value);
void shader_set_var_int(struct shader_prog *p, enum shader_vars var, int value);
#define shader_setup_attribute(_p, _v, _b, args...) \
    _shader_setup_attribute((_p), (_v), (_b), &(buffer_init_options){ args })
cerr _shader_setup_attribute(struct shader_prog *p, enum shader_vars var, buffer_t *buf,
                             const buffer_init_options *opts);
void shader_plug_attribute(struct shader_prog *p, enum shader_vars var, buffer_t *buf);
void shader_unplug_attribute(struct shader_prog *p, enum shader_vars var, buffer_t *buf);
int shader_get_texture_slot(struct shader_prog *p, enum shader_vars var);
void shader_set_texture(struct shader_prog *p, enum shader_vars var);
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
