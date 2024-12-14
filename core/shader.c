// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "common.h"
//#include "matrix.h"
#include "display.h"
#include "util.h"
#include "object.h"
#include "shader.h"
#include "librarian.h"
#include "scene.h"

static GLuint load_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    if (!shader) {
        err("couldn't create shader\n");
        return -1;
    }
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen) {
            char *buf = malloc(infoLen);
            if (buf) {
                glGetShaderInfoLog(shader, infoLen, NULL, buf);
                err("Could not Compile Shader %d:\n%s\n", type, buf);
                free(buf);
                err("--> %s <--\n", source);
            }
            glDeleteShader(shader);
            shader = 0;
        }
    }
    return shader;
}

static int shader_prog_init(struct shader_prog *p, const char *vertex,
                            const char *geometry, const char *fragment)
{
    p->vert = load_shader(GL_VERTEX_SHADER, vertex);
    p->geom =
#ifndef CONFIG_BROWSER
        geometry ? load_shader(GL_GEOMETRY_SHADER, geometry) : 0;
#else
        0;
#endif
    p->frag = load_shader(GL_FRAGMENT_SHADER, fragment);
    p->prog = glCreateProgram();
    GLint linkStatus = GL_FALSE;
    int ret = -1;

    if (!p->vert || (geometry && !p->geom) || !p->frag || !p->prog) {
        err("vshader: %d gshader: %d fshader: %d program: %d\n",
            p->vert, p->geom, p->frag, p->prog);
        return ret;
    }

    glAttachShader(p->prog, p->vert);
    glAttachShader(p->prog, p->frag);
    if (p->geom)
        glAttachShader(p->prog, p->geom);
    glLinkProgram(p->prog);
    glGetProgramiv(p->prog, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        GLint bufLength = 0;
        glGetProgramiv(p->prog, GL_INFO_LOG_LENGTH, &bufLength);
        if (bufLength) {
            char *buf = malloc(bufLength);
            if (buf) {
                glGetProgramInfoLog(p->prog, bufLength, NULL, buf);
                err("Could not link program:\n%s\n", buf);
                free(buf);
                glDeleteShader(p->vert);
                glDeleteShader(p->frag);
                if (p->geom)
                    glDeleteShader(p->geom);
            }
        }
        glDeleteProgram(p->prog);
        p->prog = 0;
    } else {
        ret = 0;
    }
    dbg("vshader: %d gshader: %d fshader: %d program: %d link: %d\n",
        p->vert, p->geom, p->frag, p->prog, linkStatus);

    return ret;
}

static void shader_prog_drop(struct ref *ref)
{
    struct shader_prog *p = container_of(ref, struct shader_prog, ref);

    GL(glDeleteProgram(p->prog));
    GL(glDeleteShader(p->vert));
    GL(glDeleteShader(p->frag));
    if (p->geom)
        GL(glDeleteShader(p->geom));
    list_del(&p->entry);
    dbg("dropping shader '%s'\n", p->name);
}

DECLARE_REFCLASS(shader_prog);

struct shader_var_desc {
    const char              *name;
    enum shader_var_type    type;
    int                     texture_slot;
    unsigned int            attr_count;
};

#define SHADER_VAR(_c, _n, _t) \
    [_c] = { .name = (_n), .type = (_t), .texture_slot = -1 }
#define SHADER_TEX(_c, _n, _slot) \
    [_c] = { .name = (_n), .type = ST_INT, .texture_slot = (_slot) }
#define SHADER_ATTR(_c, _n, _t, _count) \
    [_c] = { .name = (_n), .type = (_t), .attr_count = (_count), .texture_slot = -1 }

static const struct shader_var_desc shader_var_desc[] = {
    SHADER_ATTR(ATTR_POSITION,              "position",             ST_FLOAT, 3),
    SHADER_ATTR(ATTR_NORMAL,                "normal",               ST_FLOAT, 3),
    SHADER_ATTR(ATTR_TEX,                   "tex",                  ST_FLOAT, 2),
    SHADER_ATTR(ATTR_TANGENT,               "tangent",              ST_FLOAT, 4),
    SHADER_ATTR(ATTR_JOINTS,                "joints",               ST_BYTE,  4),
    SHADER_ATTR(ATTR_WEIGHTS,               "weights",              ST_FLOAT, 4),
    SHADER_TEX(UNIFORM_MODEL_TEX,           "model_tex",            0),
    SHADER_TEX(UNIFORM_NORMAL_MAP,          "normal_map",           1),
    SHADER_TEX(UNIFORM_EMISSION_MAP,        "emission_map",         2),
    SHADER_TEX(UNIFORM_SOBEL_TEX,           "sobel_tex",            3),
    SHADER_TEX(UNIFORM_SHADOW_MAP,          "shadow_map",           4),
    SHADER_TEX(UNIFORM_SHADOW_MAP_MS,       "shadow_map_ms",        5),
    SHADER_TEX(UNIFORM_SHADOW_MAP1,         "shadow_map1",          5),
    SHADER_TEX(UNIFORM_SHADOW_MAP2,         "shadow_map2",          6),
    SHADER_TEX(UNIFORM_SHADOW_MAP3,         "shadow_map3",          7),
    SHADER_VAR(UNIFORM_WIDTH,               "width",                ST_FLOAT),
    SHADER_VAR(UNIFORM_HEIGHT,              "height",               ST_FLOAT),
    SHADER_VAR(UNIFORM_PROJ,                "proj",                 ST_MAT4),
    SHADER_VAR(UNIFORM_VIEW,                "view",                 ST_MAT4),
    SHADER_VAR(UNIFORM_TRANS,               "trans",                ST_MAT4),
    SHADER_VAR(UNIFORM_INVERSE_VIEW,        "inverse_view",         ST_MAT4),
    SHADER_VAR(UNIFORM_LIGHT_POS,           "light_pos",            ST_VEC3),
    SHADER_VAR(UNIFORM_LIGHT_COLOR,         "light_color",          ST_VEC3),
    SHADER_VAR(UNIFORM_LIGHT_DIR,           "light_dir",            ST_VEC3),
    SHADER_VAR(UNIFORM_ATTENUATION,         "attenuation",          ST_VEC3),
    SHADER_VAR(UNIFORM_SHINE_DAMPER,        "shine_damper",         ST_FLOAT),
    SHADER_VAR(UNIFORM_REFLECTIVITY,        "reflectivity",         ST_FLOAT),
    SHADER_VAR(UNIFORM_HIGHLIGHT_COLOR,     "highlight_color",      ST_VEC4),
    SHADER_VAR(UNIFORM_IN_COLOR,            "in_color",             ST_VEC4),
    SHADER_VAR(UNIFORM_COLOR_PASSTHROUGH,   "color_passthrough",    ST_INT),
    SHADER_VAR(UNIFORM_SHADOW_MVP,          "shadow_mvp",           ST_MAT4),
    SHADER_VAR(UNIFORM_CASCADE_DISTANCES,   "cascade_distances",    ST_FLOAT),
    SHADER_VAR(UNIFORM_SHADOW_OUTLINE,      "shadow_outline",       ST_INT),
    SHADER_VAR(UNIFORM_ENTITY_HASH,         "entity_hash",          ST_INT),
    SHADER_VAR(UNIFORM_USE_NORMALS,         "use_normals",          ST_INT),
    SHADER_VAR(UNIFORM_USE_SKINNING,        "use_skinning",         ST_INT),
    SHADER_VAR(UNIFORM_USE_MSAA,            "use_msaa",             ST_INT),
    SHADER_VAR(UNIFORM_ALBEDO_TEXTURE,      "albedo_texture",       ST_INT),
    SHADER_VAR(UNIFORM_JOINT_TRANSFORMS,    "joint_transforms",     ST_MAT4),
};

static GLuint var_type[] = {
    [ST_FLOAT]  = GL_FLOAT,
    [ST_INT]    = GL_INT,
    [ST_BYTE]   = GL_UNSIGNED_BYTE,
    [ST_VEC3]   = GL_FLOAT,
    [ST_VEC4]   = GL_FLOAT,
    [ST_MAT4]   = GL_FLOAT,
};

const char *shader_get_var_name(enum shader_vars var)
{
    if (var >= SHADER_VAR_MAX)
        return "<none>";

    return shader_var_desc[var].name;
}

static void shader_prog_link(struct shader_prog *p)
{
    int i;

    dbg("program '%s' attrs/uniforms\n", p->name);
    for (i = 0; i < SHADER_VAR_MAX; i++) {
        const struct shader_var_desc *desc = &shader_var_desc[i];

        if (i < ATTR_MAX)
            p->vars[i] = glGetAttribLocation(p->prog, desc->name);
        else
            p->vars[i] = glGetUniformLocation(p->prog, desc->name);
        if (p->vars[i] >= 0)
            dbg(" -> %s %s: %d\n", i < ATTR_MAX ? "attribute" : "uniform", desc->name, p->vars[i]);
    }
}

bool shader_has_var(struct shader_prog *p, enum shader_vars var)
{
    if (var >= SHADER_VAR_MAX)
        return false;

    return p->vars[var] >= 0;
}

void shader_set_var_ptr(struct shader_prog *p, enum shader_vars var,
                        unsigned int count, void *value)
{
    if (!shader_has_var(p, var))
        return;

    const struct shader_var_desc *desc = &shader_var_desc[var];
    switch (desc->type) {
        case ST_FLOAT: {
            GL(glUniform1fv(p->vars[var], count, value));
            break;
        }
        case ST_INT: {
            GL(glUniform1iv(p->vars[var], count, value));
            break;
        }
        case ST_VEC3: {
            float *_value = value;
            GL(glUniform3fv(p->vars[var], count, _value));
            break;
        }
        case ST_VEC4: {
            float *_value = value;
            GL(glUniform4fv(p->vars[var], count, _value));
            break;
        }
        case ST_MAT4: {
            float *_value = value;
            GL(glUniformMatrix4fv(p->vars[var], count, GL_FALSE, _value));
            break;
        }
        default:
            break;
    }
}

void shader_set_var_float(struct shader_prog *p, enum shader_vars var, float value)
{
    shader_set_var_ptr(p, var, 1, &value);
}

void shader_set_var_int(struct shader_prog *p, enum shader_vars var, int value)
{
    shader_set_var_ptr(p, var, 1, &value);
}

void shader_setup_attribute(struct shader_prog *p, enum shader_vars var)
{
    if (!shader_has_var(p, var))
        return;

    const struct shader_var_desc *desc = &shader_var_desc[var];

    GL(glVertexAttribPointer(p->vars[var], desc->attr_count, var_type[desc->type], GL_FALSE, 0, (void *)0));
}

void shader_plug_attribute(struct shader_prog *p, enum shader_vars var, unsigned int buffer)
{
    if (!shader_has_var(p, var) || !buffer)
        return;

    GL(glBindBuffer(GL_ARRAY_BUFFER, buffer));
    shader_setup_attribute(p, var);
    GL(glEnableVertexAttribArray(p->vars[var]));
}

void shader_unplug_attribute(struct shader_prog *p, enum shader_vars var)
{
    if (!shader_has_var(p, var))
        return;

    GL(glDisableVertexAttribArray(p->vars[var]));
}

int shader_get_texture_slot(struct shader_prog *p, enum shader_vars var)
{
    if (!shader_has_var(p, var))
        return -1;

    return shader_var_desc[var].texture_slot;
}

void shader_set_texture(struct shader_prog *p, enum shader_vars var)
{
    const struct shader_var_desc *desc = &shader_var_desc[var];

    if (!shader_has_var(p, var))
        return;

    GL(glUniform1i(p->vars[var], desc->texture_slot));
}

void shader_plug_texture(struct shader_prog *p, enum shader_vars var, texture_t *tex)
{
    if (!shader_has_var(p, var) || !texture_loaded(tex))
        return;

    const struct shader_var_desc *desc = &shader_var_desc[var];

    texture_bind(tex, desc->texture_slot);
    shader_set_texture(p, var);
}

void shader_plug_textures_multisample(struct shader_prog *p, bool multisample,
                                      enum shader_vars tex_var, enum shader_vars ms_var,
                                      texture_t *ms_tex)
{
    if (multisample) {
        shader_plug_texture(p, ms_var, ms_tex);
        shader_plug_texture(p, tex_var, &white_pixel);
    } else {
        shader_plug_texture(p, ms_var, &white_pixel);
        shader_plug_texture(p, tex_var, ms_tex);
    }
}

struct shader_prog *
shader_prog_from_strings(const char *name, const char *vsh, const char *gsh, const char *fsh)
{
    struct shader_prog *p;

    p = ref_new(shader_prog);
    if (!p)
        return NULL;

    list_init(&p->entry);
    p->name = name;
    int err = shader_prog_init(p, vsh, gsh, fsh);
    if (err) {
        err("couldn't create program '%s'\n", name);
        ref_put(p);
        return NULL;
    }

    shader_prog_use(p);
    shader_prog_link(p);
    shader_prog_done(p);
    if (!shader_has_var(p, ATTR_POSITION)) {
        err("program '%s' doesn't have position attribute\n", p->name);
        ref_put_last(p);
        return NULL;
    }
    return p;
}

void shader_prog_use(struct shader_prog *p)
{
    ref_get(p);
    glUseProgram(p->prog);
}

void shader_prog_done(struct shader_prog *p)
{
    glUseProgram(0);
    ref_put(p);
}

struct shader_prog *shader_prog_find(struct list *shaders, const char *name)
{
    struct shader_prog *prog;

    list_for_each_entry(prog, shaders, entry)
        if (!strcmp(prog->name, name))
            return ref_get(prog);

    return NULL;
}

void shaders_free(struct list *shaders)
{
    struct shader_prog *prog, *iter;

    list_for_each_entry_iter(prog, iter, shaders, entry)
        ref_put(prog);
}

int lib_request_shaders(const char *name, struct list *shaders)
{
    LOCAL(lib_handle, hv);
    LOCAL(lib_handle, hf);
    LOCAL(lib_handle, hg);
    LOCAL(char, nvert);
    LOCAL(char, nfrag);
    LOCAL(char, ngeom);
    char *vert;
    char *frag;
    char *geom;
    struct shader_prog *p;
    size_t vsz, fsz, gsz;

    CHECK(asprintf(&nvert, "%s.vert", name));
    CHECK(asprintf(&nfrag, "%s.frag", name));
    CHECK(asprintf(&ngeom, "%s.geom", name));
    hv = lib_read_file(RES_SHADER, nvert, (void **)&vert, &vsz);
    hf = lib_read_file(RES_SHADER, nfrag, (void **)&frag, &fsz);
    hg = lib_read_file(RES_SHADER, ngeom, (void **)&geom, &gsz);

    if (!hv || !hf)
        return -1;

    p = shader_prog_from_strings(name, vert, hg ? geom : NULL, frag);
    if (!p)
        return -1;

    list_append(shaders, &p->entry);

    return 0;
}
