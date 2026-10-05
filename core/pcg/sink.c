// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include "memory.h"
#include "primitives.h"
#include "util.h"
#include "pcg/sink.h"

/**
 * struct pcg_part - one mesh of a bucket
 * @mesh:       the mesh, the sink's reference
 * @name:       the mesh's name, owned here: meshes borrow theirs
 * @reserved:   vertices reserved in @mesh so far
 * @cap:        the room @mesh is given, in vertices; grows in doublings
 */
struct pcg_part {
    struct mesh *mesh;
    char        *name;
    size_t      reserved;
    size_t      cap;
};

/**
 * struct pcg_bucket - the meshes of one key
 * @key:    what they share
 * @parts:  the meshes, the last one filling up
 */
typedef struct pcg_bucket {
    pcg_key                     key;
    darray(struct pcg_part,     parts);
} pcg_bucket;
cresp_ret(pcg_bucket);

/* a recorded label and the text it owns */
struct pcg_label_rec {
    pcg_label   label;
    char        *text;
};

struct pcg_sink {
    char                                *name;
    darray(pcg_bucket,                  buckets);
    darray(struct pcg_label_rec,        labels);
};

/* surfaces resolve to materials they don't own */
typedef const pcg_material const_pcg_material;
cresp_ret(const_pcg_material);

static const char *const pcg_role_names[PCG_ROLE_NR] = {
    [PCG_ROLE_STRUCTURE]    = "structure",
    [PCG_ROLE_FLOOR]        = "floor",
    [PCG_ROLE_CLADDING]     = "cladding",
    [PCG_ROLE_RAIL]         = "rail",
    [PCG_ROLE_METAL]        = "metal",
    [PCG_ROLE_GLASS]        = "glass",
    [PCG_ROLE_CARPET]       = "carpet",
    [PCG_ROLE_FIXTURE]      = "fixture",
    [PCG_ROLE_DIFFUSER]     = "diffuser",
    [PCG_ROLE_SIGN]         = "sign",
    [PCG_ROLE_FURNITURE]    = "furniture",
    [PCG_ROLE_SCREEN]       = "screen",
    [PCG_ROLE_BOARD]        = "board",
};

static const char *const pcg_collide_names[PCG_COLLIDE_NR] = {
    [PCG_COLLIDE_NONE]      = "none",
    [PCG_COLLIDE_SOLID]     = "solid",
    [PCG_COLLIDE_HAZARD]    = "hazard",
};

static const char *const pcg_uv_mode_names[PCG_UV_NR] = {
    [PCG_UV_METERS]         = "meters",
    [PCG_UV_UNIT]           = "unit",
};

DEFINE_CLEANUP(pcg_sink, if (*p) pcg_sink_free(*p))

cresp(pcg_sink) pcg_sink_new(const char *name)
{
    if (!name)  return cresp_error(pcg_sink, CERR_INVALID_ARGUMENTS);

    LOCAL_SET(pcg_sink, s) = mem_alloc(sizeof(*s), .zero = 1);
    if (!s)     return cresp_error(pcg_sink, CERR_NOMEM);

    darray_init(s->buckets);
    darray_init(s->labels);
    CRES_RET_T(mem_asprintf(&s->name, "%s", name), pcg_sink);

    return cresp_val(pcg_sink, NOCU(s));
}

void pcg_sink_free(pcg_sink *s)
{
    if (!s) return;

    pcg_bucket *b;
    darray_for_each(b, s->buckets) {
        struct pcg_part *part;

        /* the mesh borrows its name: drop the mesh first */
        darray_for_each(part, b->parts) {
            ref_put(part->mesh);
            mem_free(part->name);
        }
        darray_clearout(b->parts);
    }
    darray_clearout(s->buckets);

    struct pcg_label_rec *rec;
    darray_for_each(rec, s->labels)
        mem_free(rec->text);
    darray_clearout(s->labels);

    mem_free(s->name);
    mem_free(s);
}

/* the material @surf wears: the explicit one, or its role's */
static cresp(const_pcg_material) pcg_surface_resolve(const pcg_ctx *ctx, const pcg_surface *surf, pcg_uv_mode uv)
{
    if (surf->skip) return cresp_error(const_pcg_material, CERR_INVALID_ARGUMENTS, .fmt = "emitting a skipped surface");
    if (surf->role >= PCG_ROLE_NR || surf->collide >= PCG_COLLIDE_NR || uv >= PCG_UV_NR)
        return cresp_error(const_pcg_material, CERR_INVALID_ARGUMENTS,
                           .fmt = "bad role %zu, collide class %zu or UV mode %zu",
                           .arg0 = (void *)(uintptr_t)surf->role, .arg1 = (void *)(uintptr_t)surf->collide,
                           .arg2 = (void *)(uintptr_t)uv);

    const pcg_material *mat = surf->mat;
    if (!mat && ctx->palette)   mat = ctx->palette->mat[surf->role];
    if (!mat)
        return cresp_error(const_pcg_material, CERR_INVALID_ARGUMENTS,
                           .fmt = "%s surface has no material and the palette has none for it",
                           .arg0 = pcg_role_names[surf->role]);

    if (!mat->name) return cresp_error(const_pcg_material, CERR_INVALID_ARGUMENTS, .fmt = "material without a name");
    if (mat->uv_mode >= PCG_UV_NR)
        return cresp_error(const_pcg_material, CERR_INVALID_ARGUMENTS, .fmt = "material '%s' has a bad UV mode",
                           .arg0 = mat->name);
    if (mat->uv_mode != uv)
        return cresp_error(const_pcg_material, CERR_INVALID_ARGUMENTS,
                           .fmt = "material '%s' takes %s UVs, the surface emits %s",
                           .arg0 = mat->name, .arg1 = pcg_uv_mode_names[mat->uv_mode],
                           .arg2 = pcg_uv_mode_names[uv]);

    return cresp_val(const_pcg_material, mat);
}

static bool pcg_key_eq(const pcg_key *a, const pcg_key *b)
{
    return a->mat == b->mat && a->chunk == b->chunk && a->collide == b->collide;
}

static cresp(pcg_bucket) pcg_sink_bucket(pcg_sink *s, const pcg_key *key)
{
    pcg_bucket *b;

    darray_for_each(b, s->buckets)
        if (pcg_key_eq(&b->key, key))   return cresp_val(pcg_bucket, b);

    b = darray_add(s->buckets);
    if (!b)     return cresp_error(pcg_bucket, CERR_NOMEM);

    b->key = *key;
    darray_init(b->parts);

    return cresp_val(pcg_bucket, b);
}

/* a bucket's next, empty, mesh */
static cerr pcg_bucket_grow(pcg_sink *s, pcg_bucket *b)
{
    LOCAL(char, name);
    CRES_RET_CERR(mem_asprintf(&name, "%s/%s/%s/%u.%zu", s->name, b->key.mat->name,
                               pcg_collide_names[b->key.collide], b->key.chunk, darray_count(b->parts)));

    LOCAL_SET(mesh_t, m) = CRES_RET_CERR(ref_new_checked(mesh, .name = name));

    struct pcg_part *part = darray_add(b->parts);
    if (!part)  return CERR_NOMEM;

    part->mesh = NOCU(m);
    part->name = NOCU(name);

    return CERR_OK;
}

cres(pcg_target) pcg_sink_reserve(const pcg_ctx *ctx, const pcg_surface *surf, pcg_uv_mode uv, size_t nr_vx)
{
    if (!ctx || !ctx->sink || !surf || !nr_vx)  return cres_error(pcg_target, CERR_INVALID_ARGUMENTS);
    if (nr_vx > USHRT_MAX)                      return cres_error(pcg_target, CERR_TOO_LARGE);

    const pcg_material *mat = CRES_RET_SCALAR(pcg_surface_resolve(ctx, surf, uv), pcg_target);
    pcg_key key = { .mat = mat, .chunk = ctx->chunk, .collide = surf->collide };
    pcg_bucket *b = CRES_RET_SCALAR(pcg_sink_bucket(ctx->sink, &key), pcg_target);

    size_t nr_parts = darray_count(b->parts);
    struct pcg_part *part = nr_parts ? DA(b->parts, nr_parts - 1) : nullptr;

    /* before a fresh mesh takes over: the last emitter wrote past its room */
    if (part && mesh_nr_vx(part->mesh) > part->reserved)
        return cres_error(pcg_target, CERR_BUFFER_OVERRUN, .fmt = "mesh '%s' holds more than was reserved",
                          .arg0 = part->name);

    if (!part || part->reserved + nr_vx > USHRT_MAX) {
        CERR_RET_SCALAR(pcg_bucket_grow(ctx->sink, b), pcg_target);
        part = DA(b->parts, darray_count(b->parts) - 1);
    }

    size_t nr_emitted = mesh_nr_vx(part->mesh);

    size_t need = part->reserved + nr_vx;
    if (need > part->cap)   part->cap = min((size_t)USHRT_MAX, max(need, part->cap * 2));

    /*
     * Size the mesh on every reservation, not only when the room grows:
     * an emitter that calls _prim_begin() itself in between sizes the
     * mesh to exactly what it emits, trimming the room. Reallocating to
     * the size the mesh already has is cheap. The first _prim_begin() of
     * a mesh decides whether it has tangents, and this is it.
     */
    CRES_RET_SCALAR(_prim_begin(part->cap - nr_emitted, &(prim_emit_opts){ .mesh = part->mesh, .tangents = true }),
                    pcg_target);
    part->reserved = need;

    pcg_target target = { .mesh = part->mesh, .uv_swap = surf->uv_swap, .clockwise = ctx->frame.mirror };

    return cres_val(pcg_target, target);
}

cerr pcg_sink_label(const pcg_ctx *ctx, const pcg_surface *surf, const pcg_frame *at, float h, const char *text)
{
    if (!ctx || !ctx->sink || !surf || !at)     return CERR_INVALID_ARGUMENTS;
    if (!text || !*text)                        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "label without text");
    if (!(h > 0.0f))                            return CERR_INVALID_ARGUMENTS_REASON(.fmt = "label of no height");

    const pcg_material *mat = CRES_RET_CERR(pcg_surface_resolve(ctx, surf, PCG_UV_UNIT));

    LOCAL(char, copy);
    CRES_RET_CERR(mem_asprintf(&copy, "%s", text));

    struct pcg_label_rec *rec = darray_add(ctx->sink->labels);
    if (!rec)   return CERR_NOMEM;

    rec->text = NOCU(copy);
    rec->label = (pcg_label){
        .mat        = mat,
        .text       = rec->text,
        .h          = h,
        .chunk      = ctx->chunk,
        .collide    = surf->collide,
    };
    pcg_frame_compose(&ctx->frame, at, &rec->label.frame);

    return CERR_OK;
}

cerr pcg_sink_for_each_mesh(pcg_sink *s, pcg_sink_mesh_fn fn, void *priv)
{
    if (!s || !fn)  return CERR_INVALID_ARGUMENTS;

    pcg_bucket *b;
    darray_for_each(b, s->buckets) {
        struct pcg_part *part;

        darray_for_each(part, b->parts)
            if (mesh_nr_vx(part->mesh))     CERR_RET_CERR(fn(&b->key, part->mesh, priv));
    }

    return CERR_OK;
}

cerr pcg_sink_for_each_label(pcg_sink *s, pcg_sink_label_fn fn, void *priv)
{
    if (!s || !fn)  return CERR_INVALID_ARGUMENTS;

    struct pcg_label_rec *rec;
    darray_for_each(rec, s->labels)
        CERR_RET_CERR(fn(&rec->label, priv));

    return CERR_OK;
}

pcg_sink_stats pcg_sink_get_stats(const pcg_sink *s)
{
    pcg_sink_stats stats = {};

    if (!s) return stats;

    for (size_t i = 0; i < s->buckets.da.nr_el; i++) {
        const pcg_bucket *b = &s->buckets.x[i];

        for (size_t j = 0; j < b->parts.da.nr_el; j++) {
            size_t nr_vx = mesh_nr_vx(b->parts.x[j].mesh);

            if (!nr_vx) continue;
            stats.nr_models++;
            stats.nr_vertices += nr_vx;
            if (!b->key.mat->skip_shadow)   stats.nr_casters++;
        }
    }

    /* a label is a quad on its group's atlas model */
    for (size_t i = 0; i < s->labels.da.nr_el; i++) {
        const pcg_label *l = &s->labels.x[i].label;
        bool seen = false;

        for (size_t k = 0; k < i && !seen; k++) {
            const pcg_label *o = &s->labels.x[k].label;

            seen = o->mat == l->mat && o->chunk == l->chunk && o->collide == l->collide;
        }
        stats.nr_vertices += 6;
        if (seen)   continue;

        stats.nr_models++;
        if (!l->mat->skip_shadow)   stats.nr_casters++;
    }
    stats.nr_labels = s->labels.da.nr_el;

    return stats;
}
