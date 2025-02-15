// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "common.h"
#include "render.h"
#include "util.h"
#include "object.h"
#include "shader.h"
#include "librarian.h"
#include "scene.h"

static void shader_prog_drop(struct ref *ref)
{
    struct shader_prog *p = container_of(ref, struct shader_prog, ref);

    shader_done(&p->shader);
    list_del(&p->entry);
    dbg("dropping shader '%s'\n", p->name);
}

DEFINE_REFCLASS(shader_prog);

struct shader_var_desc {
    const char              *name;
    enum data_type          type;
    int                     texture_slot;
    unsigned int            attr_count;
};

#define SHADER_VAR(_c, _n, _t) \
    [_c] = { .name = (_n), .type = (_t), .texture_slot = -1 }
#define SHADER_TEX(_c, _n, _slot) \
    [_c] = { .name = (_n), .type = DT_INT, .texture_slot = (_slot) }
#define SHADER_ATTR(_c, _n, _t, _count) \
    [_c] = { .name = (_n), .type = (_t), .attr_count = (_count), .texture_slot = -1 }

static const struct shader_var_desc shader_var_desc[] = {
    SHADER_ATTR(ATTR_POSITION,              "position",             DT_FLOAT, 3),
    SHADER_ATTR(ATTR_NORMAL,                "normal",               DT_FLOAT, 3),
    SHADER_ATTR(ATTR_TEX,                   "tex",                  DT_FLOAT, 2),
    SHADER_ATTR(ATTR_TANGENT,               "tangent",              DT_FLOAT, 4),
    SHADER_ATTR(ATTR_JOINTS,                "joints",               DT_BYTE,  4),
    SHADER_ATTR(ATTR_WEIGHTS,               "weights",              DT_FLOAT, 4),
    SHADER_TEX(UNIFORM_MODEL_TEX,           "model_tex",            0),
    SHADER_TEX(UNIFORM_NORMAL_MAP,          "normal_map",           1),
    SHADER_TEX(UNIFORM_EMISSION_MAP,        "emission_map",         2),
    SHADER_TEX(UNIFORM_SOBEL_TEX,           "sobel_tex",            3),
    SHADER_TEX(UNIFORM_SHADOW_MAP,          "shadow_map",           4),
    SHADER_TEX(UNIFORM_SHADOW_MAP_MS,       "shadow_map_ms",        5),
    SHADER_TEX(UNIFORM_SHADOW_MAP1,         "shadow_map1",          5),
    SHADER_TEX(UNIFORM_SHADOW_MAP2,         "shadow_map2",          6),
    SHADER_TEX(UNIFORM_SHADOW_MAP3,         "shadow_map3",          7),
    SHADER_VAR(UNIFORM_WIDTH,               "width",                DT_FLOAT),
    SHADER_VAR(UNIFORM_HEIGHT,              "height",               DT_FLOAT),
    SHADER_VAR(UNIFORM_PROJ,                "proj",                 DT_MAT4),
    SHADER_VAR(UNIFORM_VIEW,                "view",                 DT_MAT4),
    SHADER_VAR(UNIFORM_TRANS,               "trans",                DT_MAT4),
    SHADER_VAR(UNIFORM_INVERSE_VIEW,        "inverse_view",         DT_MAT4),
    SHADER_VAR(UNIFORM_LIGHT_POS,           "light_pos",            DT_VEC3),
    SHADER_VAR(UNIFORM_LIGHT_COLOR,         "light_color",          DT_VEC3),
    SHADER_VAR(UNIFORM_LIGHT_DIR,           "light_dir",            DT_VEC3),
    SHADER_VAR(UNIFORM_ATTENUATION,         "attenuation",          DT_VEC3),
    SHADER_VAR(UNIFORM_SHINE_DAMPER,        "shine_damper",         DT_FLOAT),
    SHADER_VAR(UNIFORM_REFLECTIVITY,        "reflectivity",         DT_FLOAT),
    SHADER_VAR(UNIFORM_HIGHLIGHT_COLOR,     "highlight_color",      DT_VEC4),
    SHADER_VAR(UNIFORM_IN_COLOR,            "in_color",             DT_VEC4),
    SHADER_VAR(UNIFORM_COLOR_PASSTHROUGH,   "color_passthrough",    DT_INT),
    SHADER_VAR(UNIFORM_SHADOW_MVP,          "shadow_mvp",           DT_MAT4),
    SHADER_VAR(UNIFORM_CASCADE_DISTANCES,   "cascade_distances",    DT_FLOAT),
    SHADER_VAR(UNIFORM_SHADOW_OUTLINE,      "shadow_outline",       DT_INT),
    SHADER_VAR(UNIFORM_ENTITY_HASH,         "entity_hash",          DT_INT),
    SHADER_VAR(UNIFORM_USE_NORMALS,         "use_normals",          DT_INT),
    SHADER_VAR(UNIFORM_USE_SKINNING,        "use_skinning",         DT_INT),
    SHADER_VAR(UNIFORM_USE_MSAA,            "use_msaa",             DT_INT),
    SHADER_VAR(UNIFORM_ALBEDO_TEXTURE,      "albedo_texture",       DT_INT),
    SHADER_VAR(UNIFORM_JOINT_TRANSFORMS,    "joint_transforms",     DT_MAT4),
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
            p->vars[i] = shader_attribute(&p->shader, desc->name);
        else
            p->vars[i] = shader_uniform(&p->shader, desc->name);
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
    uniform_set_ptr(p->vars[var], desc->type, count, value);
}

void shader_set_var_float(struct shader_prog *p, enum shader_vars var, float value)
{
    shader_set_var_ptr(p, var, 1, &value);
}

void shader_set_var_int(struct shader_prog *p, enum shader_vars var, int value)
{
    shader_set_var_ptr(p, var, 1, &value);
}

cerr _shader_setup_attribute(struct shader_prog *p, enum shader_vars var, buffer_t *buf,
                             const buffer_init_options *opts)
{
    if (!shader_has_var(p, var))
        return CERR_OK;

    return _buffer_init(buf, opts);
}

void shader_plug_attribute(struct shader_prog *p, enum shader_vars var, buffer_t *buf)
{
    if (!shader_has_var(p, var) || !buf)
        return;

    buffer_bind(buf, p->vars[var]);
}

void shader_unplug_attribute(struct shader_prog *p, enum shader_vars var, buffer_t *buf)
{
    if (!shader_has_var(p, var))
        return;

    buffer_unbind(buf, p->vars[var]);
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

    uniform_set_ptr(p->vars[var], desc->type, 1, &desc->texture_slot);
}

void shader_plug_texture(struct shader_prog *p, enum shader_vars var, texture_t *tex)
{
    if (!shader_has_var(p, var) || !texture_loaded(tex))
        return;

    const struct shader_var_desc *desc = &shader_var_desc[var];

    texture_bind(tex, desc->texture_slot);
    shader_set_texture(p, var);
}

void shader_unplug_texture(struct shader_prog *p, enum shader_vars var, texture_t *tex)
{
    if (!shader_has_var(p, var) || !texture_loaded(tex))
        return;

    const struct shader_var_desc *desc = &shader_var_desc[var];

    texture_unbind(tex, desc->texture_slot);
}

void shader_plug_textures_multisample(struct shader_prog *p, bool multisample,
                                      enum shader_vars tex_var, enum shader_vars ms_var,
                                      texture_t *ms_tex)
{
    if (multisample) {
        shader_plug_texture(p, ms_var, ms_tex);
        shader_plug_texture(p, tex_var, white_pixel());
    } else {
        shader_plug_texture(p, ms_var, white_pixel());
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
    cerr err = shader_init(&p->shader, vsh, gsh, fsh);
    if (IS_CERR(err)) {
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
    shader_use(&p->shader);
}

void shader_prog_done(struct shader_prog *p)
{
    shader_unuse(&p->shader);
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
        ref_put_last(prog);
}

cerr lib_request_shaders(const char *name, struct list *shaders)
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

    cres(int) vres = mem_asprintf(&nvert, "%s.vert", name);
    cres(int) fres = mem_asprintf(&nfrag, "%s.frag", name);
    cres(int) gres = mem_asprintf(&ngeom, "%s.geom", name);
    if (IS_CERR(vres) || IS_CERR(fres) || IS_CERR(gres))
        return CERR_NOMEM;

    hv = lib_read_file(RES_SHADER, nvert, (void **)&vert, &vsz);
    hf = lib_read_file(RES_SHADER, nfrag, (void **)&frag, &fsz);
    hg = lib_read_file(RES_SHADER, ngeom, (void **)&geom, &gsz);

    if (!hv || !hf)
        return CERR_SHADER_NOT_LOADED;

    p = shader_prog_from_strings(name, vert, hg ? geom : NULL, frag);
    if (!p)
        return CERR_INVALID_SHADER;

    list_append(shaders, &p->entry);

    return CERR_OK;
}
