// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "error.h"
#include "mesh.h"
#include "render.h"
#include "util.h"
#include "object.h"
#include "shader.h"
#include "shader_constants.h"
#include "librarian.h"

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
#define SHADER_TEX(_c, _n) \
    [_c] = { .name = __stringify(_n), .type = DT_INT, .texture_slot = (SAMPLER_BINDING_ ## _n) }
#define SHADER_ATTR(_c, _n, _t, _count) \
    [_c] = { .name = (_n), .type = (_t), .attr_count = (_count), .texture_slot = -1 }

static const struct shader_var_desc shader_var_desc[] = {
    SHADER_ATTR(ATTR_POSITION,              "position",             DT_FLOAT, 3),
    SHADER_ATTR(ATTR_NORMAL,                "normal",               DT_FLOAT, 3),
    SHADER_ATTR(ATTR_TEX,                   "tex",                  DT_FLOAT, 2),
    SHADER_ATTR(ATTR_TANGENT,               "tangent",              DT_FLOAT, 4),
    SHADER_ATTR(ATTR_JOINTS,                "joints",               DT_BYTE,  4),
    SHADER_ATTR(ATTR_WEIGHTS,               "weights",              DT_FLOAT, 4),
    /* texture bindings */
    SHADER_TEX(UNIFORM_MODEL_TEX,           model_tex),
    SHADER_TEX(UNIFORM_NORMAL_MAP,          normal_map),
    SHADER_TEX(UNIFORM_EMISSION_MAP,        emission_map),
    SHADER_TEX(UNIFORM_SOBEL_TEX,           sobel_tex),
    SHADER_TEX(UNIFORM_SHADOW_MAP,          shadow_map),
    SHADER_TEX(UNIFORM_SHADOW_MAP_MS,       shadow_map_ms),
    SHADER_TEX(UNIFORM_SHADOW_MAP1,         shadow_map1),
    SHADER_TEX(UNIFORM_SHADOW_MAP2,         shadow_map2),
    SHADER_TEX(UNIFORM_SHADOW_MAP3,         shadow_map3),
    SHADER_TEX(UNIFORM_LUT_TEX,             lut_tex),
    /* "projview" uniform buffer */
    SHADER_VAR(UNIFORM_PROJ,                "proj",                 DT_MAT4),
    SHADER_VAR(UNIFORM_VIEW,                "view",                 DT_MAT4),
    SHADER_VAR(UNIFORM_INVERSE_VIEW,        "inverse_view",         DT_MAT4),
    /* "transform" uniform buffer */
    SHADER_VAR(UNIFORM_TRANS,               "trans",                DT_MAT4),
    /* "lighting" uniform buffer */
    SHADER_ARR(UNIFORM_LIGHT_POS,           "light_pos",            DT_VEC3, LIGHTS_MAX),
    SHADER_ARR(UNIFORM_LIGHT_COLOR,         "light_color",          DT_VEC3, LIGHTS_MAX),
    SHADER_ARR(UNIFORM_LIGHT_DIR,           "light_dir",            DT_VEC3, LIGHTS_MAX),
    SHADER_ARR(UNIFORM_ATTENUATION,         "attenuation",          DT_VEC3, LIGHTS_MAX),
    SHADER_ARR(UNIFORM_LIGHT_DIRECTIONAL,   "light_directional",    DT_INT, LIGHTS_MAX),
    SHADER_VAR(UNIFORM_NR_LIGHTS,           "nr_lights",            DT_INT),
    SHADER_VAR(UNIFORM_LIGHT_AMBIENT,       "light_ambient",        DT_VEC3),
    SHADER_VAR(UNIFORM_USE_NORMALS,         "use_normals",          DT_INT),
    /* "material" uniform buffer */
    SHADER_VAR(UNIFORM_SHINE_DAMPER,        "shine_damper",         DT_FLOAT),
    SHADER_VAR(UNIFORM_REFLECTIVITY,        "reflectivity",         DT_FLOAT),
    SHADER_VAR(UNIFORM_ROUGHNESS,           "roughness",            DT_FLOAT),
    SHADER_VAR(UNIFORM_METALLIC,            "metallic",             DT_FLOAT),
    SHADER_VAR(UNIFORM_ROUGHNESS_CEIL,      "roughness_ceil",       DT_FLOAT),
    SHADER_VAR(UNIFORM_ROUGHNESS_AMP,       "roughness_amp",        DT_FLOAT),
    SHADER_VAR(UNIFORM_ROUGHNESS_OCT,       "roughness_oct",        DT_INT),
    SHADER_VAR(UNIFORM_ROUGHNESS_SCALE,     "roughness_scale",      DT_FLOAT),
    SHADER_VAR(UNIFORM_METALLIC_CEIL,       "metallic_ceil",        DT_FLOAT),
    SHADER_VAR(UNIFORM_METALLIC_AMP,        "metallic_amp",         DT_FLOAT),
    SHADER_VAR(UNIFORM_METALLIC_OCT,        "metallic_oct",         DT_INT),
    SHADER_VAR(UNIFORM_METALLIC_SCALE,      "metallic_scale",       DT_FLOAT),
    SHADER_VAR(UNIFORM_METALLIC_MODE,       "metallic_mode",        DT_INT),
    SHADER_VAR(UNIFORM_SHARED_SCALE,        "shared_scale",         DT_INT),
    /* "color_pt" uniform buffer */
    SHADER_VAR(UNIFORM_IN_COLOR,            "in_color",             DT_VEC4),
    SHADER_VAR(UNIFORM_COLOR_PASSTHROUGH,   "color_passthrough",    DT_INT),
    /* "shadow" uniform buffer */
    SHADER_VAR(UNIFORM_SHADOW_VSM,          "shadow_vsm",           DT_INT),
    SHADER_ARR(UNIFORM_SHADOW_MVP,          "shadow_mvp",           DT_MAT4, CASCADES_MAX),
    SHADER_ARR(UNIFORM_CASCADE_DISTANCES,   "cascade_distances",    DT_FLOAT, CASCADES_MAX),
    SHADER_VAR(UNIFORM_SHADOW_TINT,         "shadow_tint",          DT_VEC3),
    SHADER_VAR(UNIFORM_SHADOW_OUTLINE,      "shadow_outline",       DT_INT),
    SHADER_VAR(UNIFORM_SHADOW_OUTLINE_THRESHOLD, "shadow_outline_threshold", DT_FLOAT),
    /* "skinning" uniform buffer */
    SHADER_VAR(UNIFORM_USE_SKINNING,        "use_skinning",         DT_INT),
    SHADER_ARR(UNIFORM_JOINT_TRANSFORMS,    "joint_transforms",     DT_MAT4, JOINTS_MAX),
    /* "particles" uniform buffer */
    SHADER_ARR(UNIFORM_PARTICLE_POS,        "particle_pos",         DT_VEC3, PARTICLES_MAX),
    /* "render_common" uniform buffer */
    SHADER_VAR(UNIFORM_USE_MSAA,            "use_msaa",             DT_INT),
    SHADER_VAR(UNIFORM_USE_HDR,             "use_hdr",              DT_INT),
    /* "outline" uniform buffer */
    SHADER_VAR(UNIFORM_OUTLINE_EXCLUDE,     "outline_exclude",      DT_INT),
    SHADER_VAR(UNIFORM_SOBEL_SOLID,         "sobel_solid",          DT_INT),
    SHADER_VAR(UNIFORM_SOBEL_SOLID_ID,      "sobel_solid_id",       DT_FLOAT),
    /* "bloom" uniform buffer */
    SHADER_VAR(UNIFORM_BLOOM_EXPOSURE,      "bloom_exposure",       DT_FLOAT),
    SHADER_VAR(UNIFORM_BLOOM_INTENSITY,     "bloom_intensity",      DT_FLOAT),
    SHADER_VAR(UNIFORM_BLOOM_THRESHOLD,     "bloom_threshold",      DT_FLOAT),
    SHADER_VAR(UNIFORM_BLOOM_OPERATOR,      "bloom_operator",       DT_FLOAT),
    /* "postproc" uniform buffer */
    SHADER_VAR(UNIFORM_WIDTH,               "width",                DT_FLOAT),
    SHADER_VAR(UNIFORM_HEIGHT,              "height",               DT_FLOAT),
    SHADER_VAR(UNIFORM_NEAR_PLANE,          "near_plane",           DT_FLOAT),
    SHADER_VAR(UNIFORM_FAR_PLANE,           "far_plane",            DT_FLOAT),
    SHADER_VAR(UNIFORM_LAPLACE_KERNEL,      "laplace_kernel",       DT_INT),
    SHADER_VAR(UNIFORM_USE_SSAO,            "use_ssao",             DT_INT),
    SHADER_ARR(UNIFORM_SSAO_KERNEL,         "ssao_kernel",          DT_VEC3, SSAO_KERNEL_SIZE),
    SHADER_VAR(UNIFORM_SSAO_NOISE_SCALE,    "ssao_noise_scale",     DT_VEC2),
    SHADER_VAR(UNIFORM_SSAO_RADIUS,         "ssao_radius",          DT_FLOAT),
    SHADER_VAR(UNIFORM_SSAO_WEIGHT,         "ssao_weight",          DT_FLOAT),
    SHADER_VAR(UNIFORM_LIGHTING_EXPOSURE,   "lighting_exposure",    DT_FLOAT),
    SHADER_VAR(UNIFORM_LIGHTING_OPERATOR,   "lighting_operator",    DT_FLOAT),
    SHADER_VAR(UNIFORM_CONTRAST,            "contrast",             DT_FLOAT),
    SHADER_VAR(UNIFORM_FOG_NEAR,            "fog_near",             DT_FLOAT),
    SHADER_VAR(UNIFORM_FOG_FAR,             "fog_far",              DT_FLOAT),
    SHADER_VAR(UNIFORM_FOG_COLOR,           "fog_color",            DT_VEC3),
};

/* Runtime handle for a variable block (uniform buffer) */
struct shader_var_block {
    uniform_buffer_t    ub;
    binding_points_t    binding_points;
    darray(size_t, offsets);
    const struct shader_var_block_desc *desc;
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
    [UBO_BINDING_ ## _n] = { \
        .name       = __stringify(_n), \
        .binding    = (UBO_BINDING_ ## _n), \
        .stages     = (_stages), \
        .vars       = (enum shader_vars[]){ args, SHADER_VAR_MAX }, \
    }

/* Variable block table */
static const struct shader_var_block_desc shader_var_block_desc[] = {
    DEFINE_SHADER_VAR_BLOCK(color_pt, SHADER_STAGE_FRAGMENT_BIT,
                            UNIFORM_IN_COLOR,
                            UNIFORM_COLOR_PASSTHROUGH),
    DEFINE_SHADER_VAR_BLOCK(lighting, SHADER_STAGE_VERTEX_BIT | SHADER_STAGE_FRAGMENT_BIT,
                            UNIFORM_LIGHT_POS,
                            UNIFORM_LIGHT_COLOR,
                            UNIFORM_LIGHT_DIR,
                            UNIFORM_ATTENUATION,
                            UNIFORM_LIGHT_DIRECTIONAL,
                            UNIFORM_NR_LIGHTS,
                            UNIFORM_USE_NORMALS,
                            UNIFORM_LIGHT_AMBIENT),
    DEFINE_SHADER_VAR_BLOCK(shadow, SHADER_STAGE_GEOMETRY_BIT | SHADER_STAGE_FRAGMENT_BIT,
                            UNIFORM_SHADOW_MVP,
                            UNIFORM_CASCADE_DISTANCES,
                            UNIFORM_SHADOW_TINT,
                            UNIFORM_SHADOW_VSM,
                            UNIFORM_SHADOW_OUTLINE,
                            UNIFORM_SHADOW_OUTLINE_THRESHOLD),
    DEFINE_SHADER_VAR_BLOCK(transform, SHADER_STAGE_VERTEX_BIT,
                            UNIFORM_TRANS),
    DEFINE_SHADER_VAR_BLOCK(projview, SHADER_STAGE_VERTEX_BIT | SHADER_STAGE_FRAGMENT_BIT,
                            UNIFORM_PROJ,
                            UNIFORM_VIEW,
                            UNIFORM_INVERSE_VIEW),
    DEFINE_SHADER_VAR_BLOCK(skinning, SHADER_STAGE_VERTEX_BIT,
                            UNIFORM_USE_SKINNING,
                            UNIFORM_JOINT_TRANSFORMS),
    DEFINE_SHADER_VAR_BLOCK(particles, SHADER_STAGE_VERTEX_BIT,
                            UNIFORM_PARTICLE_POS),
    DEFINE_SHADER_VAR_BLOCK(material, SHADER_STAGE_FRAGMENT_BIT,
                            UNIFORM_REFLECTIVITY,
                            UNIFORM_SHINE_DAMPER,
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
                            UNIFORM_SHARED_SCALE),
    DEFINE_SHADER_VAR_BLOCK(render_common, SHADER_STAGE_FRAGMENT_BIT,
                            UNIFORM_USE_MSAA,
                            UNIFORM_USE_HDR),
    DEFINE_SHADER_VAR_BLOCK(outline, SHADER_STAGE_FRAGMENT_BIT,
                            UNIFORM_OUTLINE_EXCLUDE,
                            UNIFORM_SOBEL_SOLID,
                            UNIFORM_SOBEL_SOLID_ID),
    DEFINE_SHADER_VAR_BLOCK(bloom, SHADER_STAGE_FRAGMENT_BIT,
                            UNIFORM_BLOOM_EXPOSURE,
                            UNIFORM_BLOOM_INTENSITY,
                            UNIFORM_BLOOM_THRESHOLD,
                            UNIFORM_BLOOM_OPERATOR),
    DEFINE_SHADER_VAR_BLOCK(postproc, SHADER_STAGE_FRAGMENT_BIT,
                            UNIFORM_WIDTH,
                            UNIFORM_HEIGHT,
                            UNIFORM_NEAR_PLANE,
                            UNIFORM_FAR_PLANE,
                            UNIFORM_SSAO_KERNEL,
                            UNIFORM_SSAO_NOISE_SCALE,
                            UNIFORM_SSAO_RADIUS,
                            UNIFORM_SSAO_WEIGHT,
                            UNIFORM_USE_SSAO,
                            UNIFORM_LAPLACE_KERNEL,
                            UNIFORM_CONTRAST,
                            UNIFORM_LIGHTING_EXPOSURE,
                            UNIFORM_LIGHTING_OPERATOR,
                            UNIFORM_FOG_COLOR,
                            UNIFORM_FOG_NEAR,
                            UNIFORM_FOG_FAR),
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
        var_block->desc = desc;

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

            err = uniform_buffer_set(ub, var_desc->type, poffset, &size, var_desc->elem_count, NULL);
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

struct shader_prog {
    struct shader_var_block *var_blocks[array_size(shader_var_block_desc)];
    shader_context          *ctx;
    const char              *name;
    uniform_t               vars[SHADER_VAR_MAX];
    shader_t                shader;

    /*
     * mesh attributes (enum mesh_attrs) and their corresponding offsets within
     * a combined vertex element of a flat buffer with all attributes interleaved
     *
     * mesh_attrs[] needs one extra slot for terminator
     * stride is the total size of all attributes of one vertex
     */
    enum mesh_attrs         mesh_attrs[ATTR_MAX + 1];
    size_t                  attr_sizes[ATTR_MAX];
    size_t                  attr_offs[ATTR_MAX];
    unsigned int            stride;
    unsigned int            nr_attrs;

    struct ref              ref;
    struct list             entry;
};

const char *shader_name(struct shader_prog *p)
{
    return p->name;
}

static struct shader_var_block *
shader_get_var_block_by_binding(struct shader_prog *p, int binding)
{
    if (binding < 0 || binding >= array_size(shader_var_block_desc))
        return NULL;

    return p->var_blocks[binding];
}

static struct shader_var_block *
shader_get_var_block_by_var(struct shader_prog *p, enum shader_vars var)
{
    if (var >= SHADER_VAR_MAX)
        return NULL;

    struct shader_var_block *var_block = p->ctx->vars[var].block;
    if (!var_block)
        return NULL;

    return p->var_blocks[var_block->desc->binding];
}

void shader_var_blocks_update(struct shader_prog *p)
{
    for (int i = 0; i < array_size(shader_var_block_desc); i++) {
        struct shader_var_block *var_block = shader_get_var_block_by_binding(p, i);

        /* Don't update uniform buffer on the GPU if current shader is not using it */
        if (!var_block)
            continue;

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

    for (i = 0; i < SHADER_VAR_MAX; i++) {
        const struct shader_var_desc *desc = &shader_var_desc[i];

        if (i < ATTR_MAX)
            p->vars[i] = shader_attribute(&p->shader, desc->name);
        else
            p->vars[i] = shader_uniform(&p->shader, desc->name);
    }
}

/* Check if shader has a standalone variable or an attribute */
static inline bool __shader_has_var(struct shader_prog *p, enum shader_vars var)
{
    if (var >= SHADER_VAR_MAX)
        return false;

    return p->vars[var] >= 0;
}

/* Check if shader has a variable either standalone or in a variable block */
bool shader_has_var(struct shader_prog *p, enum shader_vars var)
{
    bool ret = __shader_has_var(p, var);
    if (ret)
        return ret;

    return !!shader_get_var_block_by_var(p, var);
}

void shader_set_var_ptr(struct shader_prog *p, enum shader_vars var,
                        unsigned int count, void *value)
{
    const struct shader_var_desc *desc = &shader_var_desc[var];

    /* If a shader has a uniform @var, set it directly */
    if (__shader_has_var(p, var)) {
        uniform_set_ptr(p->vars[var], desc->type, count, value);
        return;
    }

    struct shader_var_block *var_block = shader_get_var_block_by_var(p, var);
    if (!var_block)
        return;

    size_t offset = *DA(var_block->offsets, p->ctx->vars[var].var_in_block_idx);
    cerr err = uniform_buffer_set(&var_block->ub, desc->type, &offset, &offset, count, value);
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

static enum mesh_attrs attr_to_mesh_map[ATTR_MAX] = {
    [ATTR_POSITION] = MESH_VX,
    [ATTR_TEX]      = MESH_TX,
    [ATTR_NORMAL]   = MESH_NORM,
    [ATTR_TANGENT]  = MESH_TANGENTS,
    [ATTR_JOINTS]   = MESH_JOINTS,
    [ATTR_WEIGHTS]  = MESH_WEIGHTS,
};

static enum shader_vars mesh_to_attr_map[MESH_MAX] = {
    [MESH_VX]       = ATTR_POSITION,
    [MESH_TX]       = ATTR_TEX,
    [MESH_NORM]     = ATTR_NORMAL,
    [MESH_TANGENTS] = ATTR_TANGENT,
    [MESH_JOINTS]   = ATTR_JOINTS,
    [MESH_WEIGHTS]  = ATTR_WEIGHTS,
};

static void shader_setup_mesh_attrs(struct shader_prog *p)
{
    p->attr_offs[0] = 0;

    size_t prev_type_size = 0;
    enum shader_vars v;
    int i;
    for (i = 0, v = 0; v < ATTR_MAX; v++) {
        if (!shader_has_var(p, v))
            continue;

        enum mesh_attrs ma = attr_to_mesh_map[v];
        p->mesh_attrs[i] = ma;

        /*
         * calculate the total stride for all attributes of one vertex; there's no
         * mesh at this point, so we have to rely on static type information
         * relating mesh attributes
         */
        p->attr_sizes[i] = data_type_size(mesh_attr_type(ma)) * mesh_attr_comp_count(ma);
        p->stride += p->attr_sizes[i];

        if (i)
            p->attr_offs[i] = p->attr_offs[i - 1] + prev_type_size;

        prev_type_size = p->attr_sizes[i];
        i++;
    }
    p->mesh_attrs[i] = MESH_MAX;
    p->nr_attrs = i;
}

cerr shader_setup_attributes(struct shader_prog *p, buffer_t *buf, struct mesh *mesh)
{
    size_t total_size = p->stride * mesh_nr_vx(mesh);

    void *flat = CRES_RET(
        mesh_flatten(mesh, p->mesh_attrs, p->attr_sizes, p->attr_offs, p->nr_attrs, p->stride),
        return CERR_NOMEM
    );

    cerr err = CERR_OK;
    buffer_t *main = NULL;
    int i;
    for (i = 0; p->mesh_attrs[i] < MESH_MAX; i++) {
        enum mesh_attrs ma = p->mesh_attrs[i];

        CERR_RET(
            buffer_init(&buf[mesh_to_attr_map[ma]],
                .loc            = mesh_to_attr_map[ma],
                .type           = BUF_ARRAY,
                .usage          = BUF_STATIC,
                .comp_type      = mesh_attr_type(ma),
                .comp_count     = mesh_attr_comp_count(ma),
                .off            = p->attr_offs[i],
                .stride         = p->stride,
                .data           = flat,
                .size           = total_size,
                .main           = main,
            ),
            { err = __cerr; goto attr_error; }
        );

        if (p->mesh_attrs[i] == MESH_VX)
            main = &buf[mesh_to_attr_map[ma]];
    }

    mem_free(flat);

    return CERR_OK;

attr_error:
    for (int u = i; u >= 0; u--)
        buffer_deinit(&buf[u]);

    mem_free(flat);

    return err;
}

static void shader_plug_attribute(struct shader_prog *p, enum shader_vars var, buffer_t *buf)
{
    if (!__shader_has_var(p, var) || !buf)
        return;

    buffer_bind(buf, p->vars[var]);
}

static void shader_unplug_attribute(struct shader_prog *p, enum shader_vars var, buffer_t *buf)
{
    if (!__shader_has_var(p, var) || !buf)
        return;

    buffer_unbind(buf, p->vars[var]);
}

void shader_plug_attributes(struct shader_prog *p, buffer_t *buf)
{
    for (enum shader_vars v = 0; v < ATTR_MAX; v++)
        shader_plug_attribute(p, v, &buf[v]);
}

void shader_unplug_attributes(struct shader_prog *p, buffer_t *buf)
{
    for (enum shader_vars v = 0; v < ATTR_MAX; v++)
        shader_unplug_attribute(p, v, &buf[v]);
}

int shader_get_texture_slot(struct shader_prog *p, enum shader_vars var)
{
    if (!__shader_has_var(p, var))
        return -1;

    return shader_var_desc[var].texture_slot;
}

void shader_plug_texture(struct shader_prog *p, enum shader_vars var, texture_t *tex)
{
    if (!__shader_has_var(p, var))
        return;

    const struct shader_var_desc *desc = &shader_var_desc[var];

    if (texture_loaded(tex))    texture_bind(tex, desc->texture_slot);
    uniform_set_ptr(p->vars[var], desc->type, 1, &desc->texture_slot);
}

void shader_unplug_texture(struct shader_prog *p, enum shader_vars var, texture_t *tex)
{
    if (!__shader_has_var(p, var) || !texture_loaded(tex))
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
    if (!__shader_has_var(p, ATTR_POSITION)) {
        err("program '%s' doesn't have position attribute\n", p->name);
        ref_put_last(p);
        return CERR_INVALID_SHADER;
    }

    p->ctx = opts->ctx;

#ifndef CONFIG_FINAL
    for (int i = 0; i < array_size(shader_var_block_desc); i++) {
        struct shader_var_block *var_block = &p->ctx->var_blocks[i];
        const struct shader_var_block_desc *desc = var_block->desc;

        err = shader_uniform_buffer_bind(&p->shader, &var_block->binding_points, desc->name);
        if (!IS_CERR(err)) {
            p->var_blocks[desc->binding] = var_block;
            for (int j = 0; j < darray_count(var_block->offsets); j++) {
                enum shader_vars var = var_block->desc->vars[j];
                int idx = p->ctx->vars[var].var_in_block_idx;

                size_t prog_off = CRES_RET(
                    shader_uniform_offset_query(
                        &p->shader,
                        var_block->desc->name,
                        shader_get_var_name(var)
                    ),
                    continue;
                );
                size_t my_off = *DA(var_block->offsets, idx);

                if (prog_off >= 0 && prog_off != my_off) {
                    err("prog[%s] UBO[%s] var[%s] offsets don't match: %zu vs %zu\n",
                        p->name, var_block->desc->name, shader_get_var_name(var),
                        my_off, prog_off);
#ifndef CLAP_DEBUG
                    *DA(var_block->offsets, idx) = prog_off;
#endif /* CLAP_DEBUG */
                }
            }
        }
    }
#endif /* CONFIG_FINAL */

    shader_setup_mesh_attrs(p);

    return CERR_OK;
}

static void shader_prog_drop(struct ref *ref)
{
    struct shader_prog *p = container_of(ref, struct shader_prog, ref);

    shader_done(&p->shader);
    list_del(&p->entry);
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

    cresp(shader_prog) res = ref_new_checked(shader_prog,
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

cresp(shader_prog) shader_prog_find_get(shader_context *ctx, struct list *shaders, const char *name)
{
    struct shader_prog *prog = shader_prog_find(shaders, name);

    if (prog)
        return cresp_val(shader_prog, prog);

    cerr err = lib_request_shaders(ctx, name, shaders);
    if (IS_CERR(err))
        return cresp_error_cerr(shader_prog, err);

    prog = list_last_entry(shaders, struct shader_prog, entry);
    return cresp_val(shader_prog, prog);
}
