// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include "draw.h"
#include "memory.h"
#include "mesh.h"
#include "primitives.h"
#include "pcg/internal.h"
#include "pcg/labels.h"

/**
 * struct pcg_label_group - the labels that share an atlas
 * @key:    their chunk, material and collide class
 * @labels: the labels, the sink's
 */
struct pcg_label_group {
    pcg_key                     key;
    darray(const pcg_label *,   labels);
};

/**
 * struct pcg_label_groups - a sink's labels by atlas
 * @groups: the groups, in the order their first labels were recorded
 */
struct pcg_label_groups {
    darray(struct pcg_label_group, groups);
};

static void pcg_label_groups_done(struct pcg_label_groups *g)
{
    struct pcg_label_group *group;

    darray_for_each(group, g->groups)
        darray_clearout(group->labels);
    darray_clearout(g->groups);
}

typedef struct pcg_label_groups pcg_label_groups;
static DEFINE_CLEANUP_EXACT(pcg_label_groups, pcg_label_groups_done(p))

static cerr pcg_label_group_add(const pcg_label *label, void *priv)
{
    pcg_label_groups *g = priv;
    pcg_key key = { .mat = label->mat, .chunk = label->chunk, .collide = label->collide };
    struct pcg_label_group *group, *found = nullptr;

    darray_for_each(group, g->groups)
        if (group->key.mat == key.mat && group->key.chunk == key.chunk && group->key.collide == key.collide) {
            found = group;
            break;
        }

    if (!found) {
        found = darray_add(g->groups);
        if (!found)     return CERR_NOMEM;

        found->key = key;
        darray_init(found->labels);
    }

    const pcg_label **slot = darray_add(found->labels);
    if (!slot)  return CERR_NOMEM;

    *slot = label;

    return CERR_OK;
}

/**
 * struct pcg_label_prints - a group's labels printed, one canvas each
 * @prints: the canvases, in the group's label order
 */
struct pcg_label_prints {
    darray(canvas *, prints);
};

static void pcg_label_prints_done(struct pcg_label_prints *p)
{
    canvas **c;

    darray_for_each(c, p->prints)
        canvas_free(*c);
    darray_clearout(p->prints);
}

typedef struct pcg_label_prints pcg_label_prints;
static DEFINE_CLEANUP_EXACT(pcg_label_prints, pcg_label_prints_done(p))

/* the atlas, its texture and material and the quads of one group */
static cerr pcg_label_group_build(struct pcg_label_group *group, const pcg_commit_opts *opts,
                                  pcg_sink_mesh_fn fn, void *priv)
{
    const pcg_material *mat = group->key.mat;
    size_t nr = darray_count(group->labels);
    vec4 plate, ink;

    vec4_dup(plate, mat->label.plate);
    vec4_dup(ink, mat->label.ink);

    /*
     * Every label in its own size, on a grid of cells that fit the largest.
     * The cells touch, but canvas_print() keeps its ink a margin away from
     * the edges, so a linear sample at a label's edge only mixes in plate.
     */
    LOCAL_SET_EXACT(pcg_label_prints, p) = {};
    unsigned int cell_w = 0, cell_h = 0;

    darray_init(p.prints);
    for (size_t i = 0; i < nr; i++) {
        canvas **slot = darray_add(p.prints);
        if (!slot)  return CERR_NOMEM;

        *slot = nullptr;
        *slot = CRES_RET_CERR(canvas_print(opts->font, TEX_FMT_RGBA8, &(canvas_address){ .color = ink }, 0,
                                           (*DA(group->labels, i))->text));
        cell_w = max(cell_w, (*slot)->width);
        cell_h = max(cell_h, (*slot)->height);
    }

    /* about as many rows as columns of pixels */
    size_t cols = (size_t)ceilf(sqrtf((float)nr * cell_h / cell_w));
    cols = clamp(cols, (size_t)1, nr);
    size_t rows = (nr + cols - 1) / cols;
    unsigned int atlas_w = cols * cell_w, atlas_h = rows * cell_h;

    LOCAL_SET(canvas, atlas) = CRES_RET_CERR(canvas_new(TEX_FMT_RGBA8, atlas_w, atlas_h));
    canvas_fill(atlas, .color = plate);
    for (size_t i = 0; i < nr; i++)
        canvas_blit(atlas, *DA(p.prints, i), .x = (i % cols) * cell_w, .y = (i / cols) * cell_h,
                    .src_blend = BLEND_SRC_ALPHA, .dst_blend = BLEND_ONE_MINUS_SRC_ALPHA);

    texture_t *tex = CRES_RET_CERR(pcg_matlib_upload(opts->lib, atlas, TEX_CLAMP_TO_EDGE, mat->label.filter));

    LOCAL(char, name);
    CRES_RET_CERR(mem_asprintf(&name, "%s.labels.%u.%u", mat->name, group->key.chunk, group->key.collide));

    pcg_material *lettered = CRES_RET_CERR(pcg_matlib_derive(opts->lib, mat, name));
    lettered->albedo = tex;

    LOCAL_SET(mesh_t, m) = CRES_RET_CERR(ref_new_checked(mesh, .name = lettered->name));
    CRES_RET_CERR(_prim_begin(nr * 6, &(prim_emit_opts){ .mesh = m, .tangents = true }));

    for (size_t i = 0; i < nr; i++) {
        const pcg_label *label = *DA(group->labels, i);
        canvas *c = *DA(p.prints, i);
        float x = (i % cols) * cell_w, y = (i / cols) * cell_h;
        float u0 = x / atlas_w, v0 = y / atlas_h;
        float u1 = (x + c->width) / atlas_w, v1 = (y + c->height) / atlas_h;
        float h = label->h, ha = h * (float)c->width / (float)c->height * 0.5f;
        bool mirror = label->frame.mirror;

        /*
         * Canvas row 0 is the top of the text, so V runs down the quad.
         * U runs along +a: from the front the text reads left to right.
         * A mirrored frame faces the quad the other way, so U turns too.
         */
        vec4 uv = { mirror ? u0 : u1, v1, mirror ? u1 : u0, v0 };
        prim_emit_opts po = { .mesh = m, .clockwise = mirror };

        pcg_frame_face(&label->frame, (vec3){ ha, -h * 0.5f, 0.0f }, (vec3){ -ha, -h * 0.5f, 0.0f },
                       (vec3){ -ha, h * 0.5f, 0.0f }, (vec3){ ha, h * 0.5f, 0.0f }, uv, false, &po);
    }

    pcg_key key = { .mat = lettered, .chunk = group->key.chunk, .collide = group->key.collide };

    return fn(&key, m, priv);
}

cerr pcg_labels_build(pcg_sink *s, const pcg_commit_opts *opts, pcg_sink_mesh_fn fn, void *priv)
{
    if (!s || !opts || !opts->lib || !fn)   return CERR_INVALID_ARGUMENTS;

    LOCAL_SET_EXACT(pcg_label_groups, g) = {};
    darray_init(g.groups);
    CERR_RET_CERR(pcg_sink_for_each_label(s, pcg_label_group_add, &g));

    if (!darray_count(g.groups))    return CERR_OK;
    if (!opts->font)                return CERR_INVALID_ARGUMENTS_REASON(.fmt = "labels without a font");

    struct pcg_label_group *group;
    darray_for_each(group, g.groups)
        CERR_RET_CERR(pcg_label_group_build(group, opts, fn, priv));

    return CERR_OK;
}
