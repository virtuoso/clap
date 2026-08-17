// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <string.h>
#include "error.h"
#include "json.h"
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

#include "bindings/render-bindings.c"
#include "bindings/shader-variants.c"

/* Runtime shader context */
typedef struct shader_context {
    /* Renderer object to pass on to shader_init() */
    renderer_t                  *renderer;
    /* Dynamically calculated uniform block parameters */
    struct shader_var_block     var_blocks[array_size(shader_var_block_desc)];
    /* Per-variable array of their respective blocks */
    struct {
        struct shader_var_block *block;
        int                     var_in_block_idx;
    } vars[SHADER_VAR_MAX];
} shader_context;

DEFINE_CLEANUP(shader_context, if (*p) mem_free(*p));

static inline enum shader_binding_renderers shader_binding_current_backend(renderer_t *r)
{
    switch (renderer_get_caps(r)->renderer) {
#ifdef CONFIG_RENDERER_OPENGL
        case RENDER_OPENGL: return RENDERER_OPENGL;
#endif /* CONFIG_RENDERER_OPENGL */
#ifdef CONFIG_RENDERER_METAL
        case RENDER_METAL:  return RENDERER_METAL;
#endif /* CONFIG_RENDERER_METAL */
#ifdef CONFIG_RENDERER_WGPU
        case RENDER_WGPU:   return RENDERER_WGPU;
#endif /* CONFIG_RENDERER_WGPU */
#ifdef CONFIG_RENDERER_VULKAN
        case RENDER_VULKAN: return RENDERER_VULKAN;
#endif /* CONFIG_RENDERER_VULKAN */
        default:            clap_unreachable();
    }

    return RENDERER_MAX;
}

static inline int shader_binding_lookup(enum shader_binding_renderers backend,
                                        enum shader_binding_classes binding_class,
                                        int binding)
{
    return shader_bindings[backend][binding_class][binding];
}

static inline int shader_binding_lookup_current(renderer_t *r, enum shader_binding_classes binding_class,
                                                int binding)
{
    return shader_binding_lookup(shader_binding_current_backend(r), binding_class, binding);
}

static inline int shader_ubo_binding(renderer_t *r, int binding)
{
    return shader_binding_lookup_current(r, BC_UBO, binding);
}

static inline int shader_texture_binding(renderer_t *r, int binding)
{
    return shader_binding_lookup_current(r, BC_TEXTURE, binding);
}

static void shader_var_block_done(shader_context *ctx, int var_idx)
{
    struct shader_var_block *var_block = &ctx->var_blocks[var_idx];
    darray_clearout(var_block->offsets);

    uniform_buffer_done(&var_block->ub);
    binding_points_done(&var_block->binding_points);
}

/* Initialize a shader context */
cresp(shader_context) shader_vars_init(renderer_t *renderer)
{
    LOCAL_SET(shader_context, ctx) = mem_alloc(sizeof(*ctx), .zero = 1);
    if (!ctx)
        return cresp_error(shader_context, CERR_NOMEM);

    cerr err = CERR_OK;
    int i, j;

    /* Instantiate shader variable blocks */
    for (i = 0; i < array_size(shader_var_block_desc); i++) {
        const struct shader_var_block_desc *desc = &shader_var_block_desc[i];
        if (!desc->name)    continue;

        struct shader_var_block *var_block = &ctx->var_blocks[i];
        size_t size = 0;

        darray_init(var_block->offsets);
        var_block->desc = desc;

        /* Initialize the uniform buffer */
        uniform_buffer_t *ub = &var_block->ub;
        int binding = shader_ubo_binding(renderer, desc->binding);
        err = uniform_buffer_init(renderer, ub, desc->name, binding);
        if (IS_CERR(err))
            goto error;

        /* Set up binding points for the uniform buffer from the stages bitmask */
        binding_points_init(&var_block->binding_points);
        for (int stage = 0; stage < SHADER_STAGES_MAX; stage++)
            if (desc->stages & (1 << stage))
                binding_points_add(&var_block->binding_points, stage, binding);

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

    ctx->renderer = renderer;

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
    for (int i = 0; i < array_size(shader_var_block_desc); i++) {
        if (!shader_var_block_desc[i].name) continue;
        shader_var_block_done(ctx, i);
    }

    mem_free(ctx);
}

struct shader_prog {
    struct shader_var_block *var_blocks[array_size(shader_var_block_desc)];
    shader_context          *ctx;
    const char              *name;
    uniform_t               vars[SHADER_VAR_MAX];
    bool                    var_cached[SHADER_VAR_MAX];
    shader_t                shader;

    /*
     * mesh attributes (enum mesh_attrs) and their corresponding offsets within
     * a combined vertex element of a flat buffer with all attributes interleaved
     *
     * mesh_attrs[] needs one extra slot for terminator
     * stride is the total size of all attributes of one vertex
     */
    enum mesh_attrs         mesh_attrs[ATTR_MAX + 1];
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

cresp(const_char) shader_get_name(const char *base, shader_variants_t variants)
{
    for (int i = 0; i < array_size(shader_variant_families); i++) {
        const struct shader_variant_family *fam = &shader_variant_families[i];
        unsigned int lo = 0, hi = fam->nr_names;

        if (strcmp(fam->base, base))    continue;

        /* names[] is sorted by mask */
        while (lo < hi) {
            unsigned int mid = (lo + hi) / 2;

            if (fam->names[mid].mask < variants)    lo = mid + 1;
            else                                    hi = mid;
        }

        if (lo == fam->nr_names || fam->names[lo].mask != variants) return cresp_error(const_char, CERR_NOT_FOUND);

        return cresp_val(const_char, fam->names[lo].name);
    }

    return cresp_error(const_char, CERR_INVALID_ARGUMENTS);
}

const char *shader_variants_str(shader_variants_t variants, char *buf, size_t size)
{
    size_t off = 0;

    buf[0] = 0;
    for (int i = 0; i < array_size(shader_variant_bit_names); i++) {
        if (!(variants & ((shader_variants_t)1 << i)))  continue;

        off += snprintf(buf + off, size - off, "%s%s", off ? "|" : "",
                        shader_variant_bit_names[i]);
        if (off >= size)    break;
    }
    if (!off)   snprintf(buf, size, "none");

    return buf;
}

static struct shader_var_block *
shader_get_var_block_by_binding(struct shader_prog *p, int binding)
{
    if (!shader_var_block_desc[binding].name || binding >= array_size(shader_var_block_desc))
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
        if (!shader_var_block_desc[i].name) continue;

        struct shader_var_block *var_block = shader_get_var_block_by_binding(p, i);

        /* Don't update uniform buffer on the GPU if current shader is not using it */
        if (!var_block)
            continue;

        uniform_buffer_update(&var_block->ub, &var_block->binding_points);
    }
}

const char *shader_get_var_name(enum shader_vars var)
{
    if (var >= SHADER_VAR_MAX)
        return "<none>";

    return shader_var_desc[var].name;
}

static void shader_fetch_var(struct shader_prog *p, enum shader_vars var)
{
    if (p->var_cached[var])         return;
    if (p->vars[var] > UA_UNKNOWN)  return;

    const struct shader_var_desc *desc = &shader_var_desc[var];

    if (var < ATTR_MAX)
        p->vars[var] = shader_attribute(&p->shader, desc->name, var);
    else
        p->vars[var] = shader_uniform(&p->shader, desc->name);

    if (p->vars[var] != UA_UNKNOWN)  p->var_cached[var] = true;
}

static void shader_prog_link(struct shader_prog *p)
{
    for (enum shader_vars i = 0; i < SHADER_VAR_MAX; i++)
        shader_fetch_var(p, i);
}

/* Check if shader has a standalone variable or an attribute */
static inline bool __shader_has_var(struct shader_prog *p, enum shader_vars var)
{
    if (var >= SHADER_VAR_MAX)
        return false;

    shader_fetch_var(p, var);

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

    /* If @var is a standalone non-opaque uniform, scream loudly and bail */
    if (__shader_has_var(p, var)) {
        err("shader '%s' has a non-opaque uniform '%s'\n", p->name, shader_get_var_name(var));
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

    size_t type_size = 0, prev_type_size = 0;
    size_t attr_comp_count[ATTR_MAX];
    data_type attr_types[ATTR_MAX];
    unsigned int attr_locs[ATTR_MAX];
    enum shader_vars v;
    int i;
    for (i = 0, v = 0; v < ATTR_MAX; v++) {
        if (!shader_has_var(p, v))
            continue;

        enum mesh_attrs ma = attr_to_mesh_map[v];
        p->mesh_attrs[i] = ma;
        /* variant progs can have gaps in their attribute locations */
        attr_locs[i] = v;

        /*
         * calculate the total stride for all attributes of one vertex; there's no
         * mesh at this point, so we have to rely on static type information
         * relating mesh attributes
         */
        attr_types[i] = mesh_attr_type(ma);
        attr_comp_count[i] = mesh_attr_comp_count(ma);
        type_size = data_type_size(attr_types[i]) * attr_comp_count[i];
        p->stride += type_size;

        if (i)
            p->attr_offs[i] = p->attr_offs[i - 1] + prev_type_size;

        prev_type_size = type_size;
        i++;
    }

    shader_set_vertex_attrs(&p->shader, p->stride, p->attr_offs, attr_types, attr_comp_count,
                            attr_locs, i);
    p->mesh_attrs[i] = MESH_MAX;
    p->nr_attrs = i;
}

cerr shader_setup_attributes(struct shader_prog *p, buffer_t *buf, struct mesh *mesh)
{
    size_t total_size = p->stride * mesh_nr_vx(mesh);

    void *flat = CRES_RET(
        mesh_flatten(mesh, p->mesh_attrs, p->attr_offs, p->nr_attrs, p->stride),
        return CERR_NOMEM
    );

    cerr err = CERR_OK;
    buffer_t *main = NULL;
    int i;
    for (i = 0; p->mesh_attrs[i] < MESH_MAX; i++) {
        enum mesh_attrs ma = p->mesh_attrs[i];

        CERR_RET(
            buffer_init(&buf[mesh_to_attr_map[ma]],
                .renderer       = p->ctx->renderer,
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

        if (p->mesh_attrs[i] == MESH_VX) {
            main = &buf[mesh_to_attr_map[ma]];
            buffer_set_name(main, "%s:vx", mesh->name);
        }
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

    return shader_texture_binding(p->ctx->renderer, shader_var_desc[var].texture_slot);
}

void shader_plug_texture(struct shader_prog *p, enum shader_vars var, texture_t *tex)
{
    if (!__shader_has_var(p, var))
        return;

    const struct shader_var_desc *desc = &shader_var_desc[var];
    int texture_slot = shader_texture_binding(p->ctx->renderer, desc->texture_slot);

    if (texture_loaded(tex))    texture_bind(tex, texture_slot, p->vars[var]);
}

void shader_unplug_texture(struct shader_prog *p, enum shader_vars var, texture_t *tex)
{
    if (!__shader_has_var(p, var) || !texture_loaded(tex))
        return;

    const struct shader_var_desc *desc = &shader_var_desc[var];
    int texture_slot = shader_texture_binding(p->ctx->renderer, desc->texture_slot);

    texture_unbind(tex, texture_slot);
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

static cerr shader_reflection_apply(struct shader_prog *p, const char *text, bool vertex_stage)
{
    LOCAL_SET(JsonNode, root) = json_decode(text);

    auto array = json_find_member(root, "textures");
    if (array && array->tag == JSON_ARRAY) {
        for (JsonNode *it = array->children.head; it; it = it->next) {
            auto jtype = json_find_member(it, "type");
            auto jname = json_find_member(it, "name");
            auto jbind = json_find_member(it, "binding");
            if (!jtype || jtype->tag != JSON_STRING ||
                !jname || jname->tag != JSON_STRING ||
                !jbind || jbind->tag != JSON_NUMBER)
                continue;

            const char *type = jtype->string_, *name = jname->string_;
            int bind = jbind->number_;

            for (enum shader_vars i = 0; i < SHADER_VAR_MAX; i++) {
                auto desc = &shader_var_desc[i];
                if (desc->texture_slot < 0)     continue;
                if (strcmp(name, desc->name))   continue;
                int texture_slot = shader_texture_binding(p->ctx->renderer, desc->texture_slot);

                if (texture_slot != bind) {
                    err("tex '%s' (%s) bindings don't match: %d <> %d\n", name, type, texture_slot, bind);
                    continue;
                }

                p->vars[i] = bind;
                p->var_cached[i] = true;
                break;
            }
        }
    }

    /* non-vertex stages' inputs are varyings, not vertex attributes */
    array = vertex_stage ? json_find_member(root, "inputs") : NULL;
    if (array && array->tag == JSON_ARRAY) {
        for (JsonNode *it = array->children.head; it; it = it->next) {
            auto jname = json_find_member(it, "name");
            auto jloc = json_find_member(it, "location");
            if (!jname || jname->tag != JSON_STRING ||
                !jloc || jloc->tag != JSON_NUMBER)
                continue;

            const char *name = jname->string_;
            unsigned int loc = jloc->number_;

            if (loc >= ATTR_MAX) {
                err("attribute '%s' has incompatible location: %u\n", name, loc);
                continue;
            }

            if (p->var_cached[loc]) continue;

            auto desc = &shader_var_desc[loc];
            if (!strcmp(name, desc->name)) {
                p->var_cached[loc] = true;
                p->vars[loc] = loc;
            }
        }
    }

    array = json_find_member(root, "ubos");
    if (array && array->tag == JSON_ARRAY) {
        for (JsonNode *it = array->children.head; it; it = it->next) {
            auto jname = json_find_member(it, "name");
            auto jbind = json_find_member(it, "binding");
            if (!jname || jname->tag != JSON_STRING ||
                !jbind || jbind->tag != JSON_NUMBER)
                continue;

            const char *name = jname->string_;
            int bind = jbind->number_;

            for (size_t i = 0; i < array_size(shader_var_block_desc); i++) {
                auto desc = &shader_var_block_desc[i];
                if (!desc->name || strcmp(name, desc->name))    continue;
                int expected_binding = shader_ubo_binding(p->ctx->renderer, desc->binding);

                if (expected_binding != bind) {
                    err("ubo '%s' bindings don't match: %d <> %d\n", name, expected_binding, bind);
                    continue;
                }

                struct shader_var_block *var_block = &p->ctx->var_blocks[i];

                /* If UBO was bound from reflection scanner, skip it */
                if (p->var_blocks[desc->binding])   continue;

                CERR_RET(shader_uniform_buffer_bind(&p->shader, &var_block->binding_points, desc->name), continue);
                p->var_blocks[desc->binding] = var_block;

                for (size_t i = 0; desc->vars[i] != SHADER_VAR_MAX; i++) {
                    enum shader_vars var = desc->vars[i];
                    p->var_cached[var] = true;
                    p->vars[var] = UA_NOT_PRESENT;
                }

                break;
            }
        }
    }

    return CERR_OK;
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
    CERR_RET_CERR(shader_init(opts->ctx->renderer, &p->shader,
                              opts->vert_text, opts->vert_size,
                              opts->geom_text, opts->geom_size,
                              opts->frag_text, opts->frag_size));

    p->ctx = opts->ctx;
    for (enum shader_vars v = 0; v < SHADER_VAR_MAX; v++)
        p->vars[v] = UA_UNKNOWN;

    CERR_RET(shader_prog_use(p, false), { shader_done(&p->shader); return __cerr; });
    shader_prog_link(p);

    cerr vert_ref_err = CERR_OK;
    cerr geom_ref_err = CERR_OK;
    cerr frag_ref_err = CERR_OK;

    if (opts->vert_ref_text)    vert_ref_err = shader_reflection_apply(p, opts->vert_ref_text, true);
    if (opts->geom_ref_text)    geom_ref_err = shader_reflection_apply(p, opts->geom_ref_text, false);
    if (opts->frag_ref_text)    frag_ref_err = shader_reflection_apply(p, opts->frag_ref_text, false);

    shader_prog_done(p, false);
    if (!__shader_has_var(p, ATTR_POSITION)) {
        err("program '%s' doesn't have position attribute\n", p->name);
        ref_put_last(p);
        return CERR_INVALID_SHADER;
    }

    if (opts->vert_ref_text && !IS_CERR(vert_ref_err) &&
        opts->frag_ref_text && !IS_CERR(frag_ref_err) &&
        (!opts->geom_text || (opts->geom_ref_text && !IS_CERR(geom_ref_err))))
        goto out;

    /*
     * Binding uniform buffers to binding points is not optional
     */
    for (int i = 0; i < array_size(shader_var_block_desc); i++) {
        if (!shader_var_block_desc[i].name) continue;

        struct shader_var_block *var_block = &p->ctx->var_blocks[i];
        const struct shader_var_block_desc *desc = var_block->desc;

        /* If UBO was bound from reflection scanner, skip it */
        if (p->var_blocks[desc->binding])   continue;

        auto err = shader_uniform_buffer_bind(&p->shader, &var_block->binding_points, desc->name);
        if (!IS_CERR(err)) {
            p->var_blocks[desc->binding] = var_block;
            for (int j = 0; j < darray_count(var_block->offsets); j++) {
                enum shader_vars var = desc->vars[j];
                p->vars[var] = UA_NOT_PRESENT;
                p->var_cached[var] = true;
            }

            /*
             * This bit is entirely optional though: making sure that
             * our own UBO offset calculations match what the shader
             * introspection reports.
             */
#ifndef CONFIG_FINAL
            for (int j = 0; j < darray_count(var_block->offsets); j++) {
                enum shader_vars var = var_block->desc->vars[j];
                int idx = p->ctx->vars[var].var_in_block_idx;

                size_t prog_off = CRES_RET(
                    shader_uniform_offset_query(
                        &p->shader,
                        var_block->desc->name,
                        shader_get_var_name(var)
                    ),
                    {
                        if (IS_CERR_CODE(__resp, CERR_NOT_FOUND))
                            err("uniform %s not found in %s\n", shader_get_var_name(var), desc->name);
                        continue;
                    }
                );
                size_t my_off = *DA(var_block->offsets, idx);

                if (prog_off >= 0 && prog_off != my_off) {
                    err("prog[%s] UBO[%s] var[%s] offsets don't match: %zu vs %zu\n",
                        p->name, var_block->desc->name, shader_get_var_name(var),
                        my_off, prog_off);
                    /* In a non-debug situation, fix up the offset */
#ifndef CLAP_DEBUG
                    *DA(var_block->offsets, idx) = prog_off;
#endif /* CLAP_DEBUG */
                }
            }
#endif /* !CONFIG_FINAL */
        }
    }

out:
    shader_set_name(&p->shader, p->name);
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

renderer_t *shader_prog_renderer(struct shader_prog *p)
{
    return p->ctx->renderer;
}

shader_t *shader_prog_shader(struct shader_prog *p)
{
    return &p->shader;
}

cerr shader_prog_use(struct shader_prog *p, bool draw)
{
    ref_get(p);

    cerr ret = shader_use(&p->shader, draw);
    if (IS_CERR(ret))   ref_put(p);

    return ret;
}

void shader_prog_done(struct shader_prog *p, bool draw)
{
    shader_unuse(&p->shader, draw);
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

/* One stage's sources for the length of a prog load: the handles own the text, this is a view */
typedef struct shader_stage_src {
    lib_handle  *code;
    lib_handle  *ref;
    const char  *text;
    const char  *ref_text;
    size_t      size;
} shader_stage_src;

/* A prog's sources, one per stage, under one scope cleanup */
typedef struct shader_srcs {
    shader_stage_src    stage[SHADER_STAGES_MAX];
} shader_srcs;

static void shader_srcs_put(shader_srcs *srcs)
{
    for (shader_stage s = 0; s < SHADER_STAGES_MAX; s++) {
        if (srcs->stage[s].code)    ref_put_last(srcs->stage[s].code);
        if (srcs->stage[s].ref)     ref_put_last(srcs->stage[s].ref);
    }
}

static DEFINE_CLEANUP_EXACT(shader_srcs, shader_srcs_put(p))

static const char *shader_stage_file_ext[SHADER_STAGES_MAX] = {
    [SHADER_STAGE_VERTEX]   = "vert",
    [SHADER_STAGE_FRAGMENT] = "frag",
    [SHADER_STAGE_GEOMETRY] = "geom",
    [SHADER_STAGE_COMPUTE]  = "comp",
};

/*
 * Read one stage of @name's prog: the compiled shader and, on the
 * backends that emit one, its reflection. A stage @name has no source
 * for leaves @src->code NULL.
 */
static cerr lib_read_stage_files(const char *ext, const char *name, shader_stage stage, shader_stage_src *src)
{
    LOCAL(char, file);
    LOCAL(char, ref_file);

    CRES_RET(mem_asprintf(&file, "%s.%s%s", name, shader_stage_file_ext[stage], ext), return CERR_NOMEM);
    CRES_RET(mem_asprintf(&ref_file, "%s.json", file), return CERR_NOMEM);

    void *text;
    src->code = lib_read_file(RES_SHADER, file, &text, &src->size);
    if (!src->code) return CERR_OK;
    src->text = text;

    void *ref_text;
    size_t ref_size;
    src->ref = lib_read_file(RES_SHADER, ref_file, &ref_text, &ref_size);
    if (src->ref)   src->ref_text = ref_text;

    return CERR_OK;
}

/*
 * Load one stage of @name's prog. A stage @name has no source for
 * leaves @src->code NULL for the caller to judge, the geometry stage
 * being optional and the fragment stage not; the vertex stage has a
 * default: the post-process progs draw a full-screen quad and do all
 * their work in the fragment stage, so one without a vertex shader
 * under its name gets passthrough.vert, reflection and all, where the
 * reflection is what the prog's attribute layout comes from.
 */
static cerr lib_read_shader_stage(shader_context *ctx, const char *name, shader_stage stage, shader_stage_src *src)
{
    const char *ext = renderer_shader_ext(ctx->renderer);

    CERR_RET_CERR(lib_read_stage_files(ext, name, stage, src));
    if (src->code || stage != SHADER_STAGE_VERTEX)  return CERR_OK;

    dbg("shader '%s': no vertex stage of its own, using passthrough.vert\n", name);
    return lib_read_stage_files(ext, "passthrough", stage, src);
}

cerr lib_request_shaders(shader_context *ctx, const char *name, struct list *shaders)
{
    LOCAL_SET_EXACT(shader_srcs, srcs) = {};
    shader_stage_src *vert = &srcs.stage[SHADER_STAGE_VERTEX];
    shader_stage_src *frag = &srcs.stage[SHADER_STAGE_FRAGMENT];
    shader_stage_src *geom = &srcs.stage[SHADER_STAGE_GEOMETRY];

    CERR_RET_CERR(lib_read_shader_stage(ctx, name, SHADER_STAGE_VERTEX, vert));
    CERR_RET_CERR(lib_read_shader_stage(ctx, name, SHADER_STAGE_FRAGMENT, frag));
    CERR_RET_CERR(lib_read_shader_stage(ctx, name, SHADER_STAGE_GEOMETRY, geom));
    /* the geometry stage is optional */
    if (!vert->code || !frag->code) return CERR_SHADER_NOT_LOADED;

    struct shader_prog *prog = CRES_RET_CERR(
        ref_new_checked(shader_prog,
                        .ctx            = ctx,
                        .name           = name,
                        .vert_text      = vert->text,
                        .geom_text      = geom->text,
                        .frag_text      = frag->text,
                        .vert_ref_text  = vert->ref_text,
                        .geom_ref_text  = geom->ref_text,
                        .frag_ref_text  = frag->ref_text,
                        .vert_size      = vert->size,
                        .geom_size      = geom->size,
                        .frag_size      = frag->size,
        )
    );
    list_append(shaders, &prog->entry);

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
