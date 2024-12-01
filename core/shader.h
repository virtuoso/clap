/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SHADER_H__
#define __CLAP_SHADER_H__

#include "display.h"
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
    UNIFORM_EMISSION_MAP,
    UNIFORM_WIDTH,
    UNIFORM_HEIGHT,
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
    UNIFORM_ENTITY_HASH,
    UNIFORM_USE_NORMALS,
    UNIFORM_USE_SKINNING,
    UNIFORM_ALBEDO_TEXTURE,
    UNIFORM_JOINT_TRANSFORMS,
    SHADER_VAR_MAX
};

enum shader_var_type {
    ST_NONE = 0,
    ST_BYTE,
    ST_INT,
    ST_FLOAT,
    ST_VEC3,
    ST_VEC4,
    ST_MAT4,
};

struct shader_prog {
    const char  *name;
    GLuint      prog;
    GLint       vars[SHADER_VAR_MAX];
    GLint       attr_count;
    struct ref  ref;
    struct list entry;
};

struct shader_prog *
shader_prog_from_strings(const char *name, const char *vsh, const char *fsh);
void shader_prog_use(struct shader_prog *p);
void shader_prog_done(struct shader_prog *p);
const char *shader_get_var_name(enum shader_vars var);
bool shader_has_var(struct shader_prog *p, enum shader_vars var);
void shader_set_var_ptr(struct shader_prog *p, enum shader_vars var,
                        unsigned int count, void *value);
void shader_set_var_float(struct shader_prog *p, enum shader_vars var, float value);
void shader_set_var_int(struct shader_prog *p, enum shader_vars var, int value);
void shader_setup_attribute(struct shader_prog *p, enum shader_vars var);
void shader_plug_attribute(struct shader_prog *p, enum shader_vars var, unsigned int buffer);
void shader_unplug_attribute(struct shader_prog *p, enum shader_vars var);
int shader_get_texture_slot(struct shader_prog *p, enum shader_vars var);
void shader_set_texture(struct shader_prog *p, enum shader_vars var);
void shader_plug_texture(struct shader_prog *p, enum shader_vars var, texture_t *tex);
struct shader_prog *shader_prog_find(struct list *shaders, const char *name);
void shaders_free(struct list *shaders);
int lib_request_shaders(const char *name, struct list *shaders);

#endif /* __CLAP_SHADER_H__ */
