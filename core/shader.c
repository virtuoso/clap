// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "common.h"
#include "error.h"
#include "render.h"
#include "util.h"
#include "object.h"
#include "shader.h"
#include "librarian.h"
#include "scene.h"

struct shader_var_desc {
    const char              *name;
    enum data_type          type;
    int                     texture_slot;
    unsigned int            attr_count;
    unsigned int            elem_count;
};

#define SHADER_VAR(_c, _n, _t) \
    [_c] = { .name = (_n), .type = (_t), .texture_slot = -1, .elem_count = 1 }
#define SHADER_ARR(_c, _n, _t, _el) \
    [_c] = { .name = (_n), .type = (_t), .texture_slot = -1, .elem_count = (_el) }
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
    SHADER_ARR(UNIFORM_LIGHT_POS,           "light_pos",            DT_VEC3, LIGHTS_MAX),
    SHADER_ARR(UNIFORM_LIGHT_COLOR,         "light_color",          DT_VEC3, LIGHTS_MAX),
    SHADER_ARR(UNIFORM_LIGHT_DIR,           "light_dir",            DT_VEC3, LIGHTS_MAX),
    SHADER_ARR(UNIFORM_ATTENUATION,         "attenuation",          DT_VEC3, LIGHTS_MAX),
    SHADER_VAR(UNIFORM_SHINE_DAMPER,        "shine_damper",         DT_FLOAT),
    SHADER_VAR(UNIFORM_REFLECTIVITY,        "reflectivity",         DT_FLOAT),
    SHADER_VAR(UNIFORM_HIGHLIGHT_COLOR,     "highlight_color",      DT_VEC4),
    SHADER_VAR(UNIFORM_IN_COLOR,            "in_color",             DT_VEC4),
    SHADER_VAR(UNIFORM_COLOR_PASSTHROUGH,   "color_passthrough",    DT_INT),
    SHADER_ARR(UNIFORM_SHADOW_MVP,          "shadow_mvp",           DT_MAT4, CASCADES_MAX),
    SHADER_ARR(UNIFORM_CASCADE_DISTANCES,   "cascade_distances",    DT_FLOAT, CASCADES_MAX),
    SHADER_VAR(UNIFORM_SHADOW_OUTLINE,      "shadow_outline",       DT_INT),
    SHADER_VAR(UNIFORM_ENTITY_HASH,         "entity_hash",          DT_INT),
    SHADER_VAR(UNIFORM_USE_NORMALS,         "use_normals",          DT_INT),
    SHADER_VAR(UNIFORM_USE_SKINNING,        "use_skinning",         DT_INT),
    SHADER_VAR(UNIFORM_USE_MSAA,            "use_msaa",             DT_INT),
    SHADER_VAR(UNIFORM_ALBEDO_TEXTURE,      "albedo_texture",       DT_INT),
    SHADER_ARR(UNIFORM_JOINT_TRANSFORMS,    "joint_transforms",     DT_MAT4, JOINTS_MAX),
};

/* Runtime handle for a variable block (uniform buffer) */
struct shader_var_block {
    uniform_buffer_t    ub;
    binding_points_t    binding_points;
    darray(size_t, offsets);
};

/* Static variable block (uniform buffer) descriptor */
struct shader_var_block_desc {
    const char          *name;
    int                 binding;
    unsigned int        stages;
    enum shader_vars    *vars;
};

/* Define a variable block: name, shader stages, a list of uniforms */
#define DEFINE_SHADER_VAR_BLOCK(_n, _stages, args...) \
    { \
        .name       = __stringify((_n)), \
        .binding    = (UBO_BINDING_ ## _n), \
        .stages     = (_stages), \
        .vars       = (enum shader_vars[]){ args, SHADER_VAR_MAX }, \
    }

/* Variable block table */
static const struct shader_var_block_desc shader_var_block_desc[] = {
};

/* Runtime shader context */
typedef struct shader_context {
    /* Dynamically calculated uniform block parameters */
    struct shader_var_block     var_blocks[array_size(shader_var_block_desc)];
    /* Per-variable array of their respective blocks */
    struct {
        struct shader_var_block *block;
        int                     var_in_block_idx;
    } vars[SHADER_VAR_MAX];
} shader_context;

DEFINE_CLEANUP(shader_context, if (*p) mem_free(*p));

static void shader_var_block_done(shader_context *ctx, int var_idx)
{
    struct shader_var_block *var_block = &ctx->var_blocks[var_idx];
    darray_clearout(var_block->offsets);

    uniform_buffer_done(&var_block->ub);
    binding_points_done(&var_block->binding_points);
}

/* Initialize a shader context */
cresp(shader_context) shader_vars_init(void)
{
    LOCAL_SET(shader_context, ctx) = mem_alloc(sizeof(*ctx), .zero = 1);
    if (!ctx)
        return cresp_error(shader_context, CERR_NOMEM);

    cerr err = CERR_OK;
    int i, j;

    /* Instantiate shader variable blocks */
    for (i = 0; i < array_size(shader_var_block_desc); i++) {
        const struct shader_var_block_desc *desc = &shader_var_block_desc[i];
        struct shader_var_block *var_block = &ctx->var_blocks[i];
        size_t size = 0;

        darray_init(var_block->offsets);

        /* Initialize the uniform buffer */
        uniform_buffer_t *ub = &var_block->ub;
        err = uniform_buffer_init(ub, desc->binding);
        if (IS_CERR(err))
            goto error;

        /* Set up binding points for the uniform buffer from the stages bitmask */
        binding_points_init(&var_block->binding_points);
        for (int stage = 0; stage < SHADER_STAGES_MAX; stage++)
            if (desc->stages & (1 << stage))
                binding_points_add(&var_block->binding_points, stage, desc->binding);

        /* Attach uniforms to a variable block */
        for (j = 0; desc->vars[j] < SHADER_VAR_MAX; j++) {
            enum shader_vars var = desc->vars[j];
            const struct shader_var_desc *var_desc = &shader_var_desc[var];

            size_t *poffset = darray_add(var_block->offsets);
            if (!poffset)
                goto error_ub_done;

            *poffset = size;

            err = uniform_buffer_set(ub, var_desc->type, &size, var_desc->elem_count, NULL);
            if (IS_CERR(err))
                goto error_ub_done;

            ctx->vars[var].block = var_block;
            ctx->vars[var].var_in_block_idx = j;
        }

        err = uniform_buffer_data_alloc(ub, size);
        if (IS_CERR(err))
            goto error_ub_done;

        err = uniform_buffer_bind(ub, &var_block->binding_points);
        if (IS_CERR(err))
            goto error_ub_done;
    }

    return cresp_val(shader_context, NOCU(ctx));

error_ub_done:
    for (; i >= 0; i--) {
        shader_var_block_done(ctx, i);
error:
    }

    return cresp_error_cerr(shader_context, err);
}

void shader_vars_done(shader_context *ctx)
{
    for (int i = 0; i < array_size(shader_var_block_desc); i++)
        shader_var_block_done(ctx, i);

    mem_free(ctx);
}

void shader_var_blocks_update(shader_context *ctx)
{
    for (int i = 0; i < array_size(shader_var_block_desc); i++) {
        struct shader_var_block *var_block = &ctx->var_blocks[i];
        cerr err = uniform_buffer_bind(&var_block->ub, &var_block->binding_points);
        if (IS_CERR(err))
            err_cerr(err, "UBO binding failed\n");
        uniform_buffer_update(&var_block->ub);
    }
}

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
    const struct shader_var_desc *desc = &shader_var_desc[var];

    /* If a shader has a uniform @var, set it directly */
    if (shader_has_var(p, var)) {
        uniform_set_ptr(p->vars[var], desc->type, count, value);
        return;
    }

    struct shader_var_block *var_block = p->ctx->vars[var].block;

    if (!var_block)
        return;

    size_t offset = *DA(var_block->offsets, p->ctx->vars[var].var_in_block_idx);
    cerr err = uniform_buffer_set(&var_block->ub, desc->type, &offset, count, value);
    if (IS_CERR(err))
        err_cerr(err, "failed to set a uniform buffer variable '%s'\n", desc->name);
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

static cerr shader_prog_make(struct ref *ref, void *_opts)
{
    rc_init_opts(shader_prog) *opts = _opts;

    if (!opts->ctx || !opts->vert_text || !opts->frag_text)
        return CERR_INVALID_ARGUMENTS;
    if (!opts->name)
        return CERR_INVALID_ARGUMENTS;

    struct shader_prog *p = container_of(ref, struct shader_prog, ref);
    list_init(&p->entry);
    p->name = opts->name;
    cerr err = shader_init(&p->shader, opts->vert_text, opts->geom_text, opts->frag_text);
    if (IS_CERR(err)) {
        err("couldn't create program '%s'\n", opts->name);
        ref_put(p);
        return cerr_error_cres(err);
    }

    shader_prog_use(p);
    shader_prog_link(p);
    shader_prog_done(p);
    if (!shader_has_var(p, ATTR_POSITION)) {
        err("program '%s' doesn't have position attribute\n", p->name);
        ref_put_last(p);
        return CERR_INVALID_SHADER;
    }

    p->ctx = opts->ctx;

    return CERR_OK;
}

static void shader_prog_drop(struct ref *ref)
{
    struct shader_prog *p = container_of(ref, struct shader_prog, ref);

    shader_done(&p->shader);
    list_del(&p->entry);
    dbg("dropping shader '%s'\n", p->name);
}

DEFINE_REFCLASS2(shader_prog);

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

cerr lib_request_shaders(shader_context *ctx, const char *name, struct list *shaders)
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

    cresp(shader_prog) res = ref_new2(shader_prog,
                                      .ctx       = ctx,
                                      .name      = name,
                                      .vert_text = vert,
                                      .geom_text = hg ? geom : NULL,
                                      .frag_text = frag);
    if (IS_CERR(res))
        return cerr_error_cres(res);

    list_append(shaders, &res.val->entry);

    return CERR_OK;
}
