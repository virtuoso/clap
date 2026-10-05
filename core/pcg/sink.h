/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_SINK_H__
#define __CLAP_PCG_SINK_H__

#include "error.h"
#include "mesh.h"
#include "prng.h"
#include "util.h"
#include "pcg/frame.h"
#include "pcg/material.h"
#include "pcg/surface.h"

/*
 * A sink collects what generators emit, batched into mesh buckets: one
 * bucket per (chunk, material, collide class), so every surface that
 * wears the same material in the same chunk and collides the same way
 * ends up in the same meshes, whichever element emitted it. Emitting is
 * pure: a sink holds meshes and label records, and turning them into
 * models and entities is a separate step over the iteration API below.
 */
typedef struct pcg_sink pcg_sink;
cresp_ret(pcg_sink);

/**
 * struct pcg_ctx - where a generator's output goes
 * @sink:       mesh buckets, see pcg_sink_new()
 * @palette:    role -> material map; nullptr: every surface names its
 *              material explicitly
 * @rng:        random stream for generators that vary; nullptr if none do
 * @frame:      the caller's frame in sink space; sites are local to it
 * @chunk:      batching partition: geometry only merges within a chunk
 *
 * Passed by pointer, copied by value into children: a parent places a
 * child with pcg_ctx_at() and the child never sees its parent's frame.
 * A parent picks the chunk; children never need to know it.
 */
typedef struct pcg_ctx {
    pcg_sink            *sink;
    const pcg_palette   *palette;
    prng                *rng;
    pcg_frame           frame;
    unsigned int        chunk;
} pcg_ctx;

/**
 * pcg_ctx_at() - the context of a child placed inside a parent's frame
 * @ctx:    the parent's context
 * @local:  the child's frame, in @ctx's local coordinates
 *
 * Return: @ctx with @local composed into its frame
 */
static inline pcg_ctx pcg_ctx_at(const pcg_ctx *ctx, const pcg_frame *local)
{
    pcg_ctx child = *ctx;

    pcg_frame_compose(&ctx->frame, local, &child.frame);

    return child;
}

/**
 * pcg_sink_new() - make an empty sink
 * @name:   prefix of the names of the sink's meshes, copied
 *
 * Return: the sink, to be pcg_sink_free()d by the caller;
 * CERR_INVALID_ARGUMENTS without a @name, or CERR_NOMEM
 */
cresp_check(pcg_sink) pcg_sink_new(const char *name);

/**
 * pcg_sink_free() - free a sink
 * @s:  the sink, may be nullptr
 *
 * Drops the sink's references to its meshes and frees its labels.
 */
void pcg_sink_free(pcg_sink *s);
DECLARE_CLEANUP(pcg_sink);

/**
 * struct pcg_target - where a reservation's vertices go
 * @mesh:       has room for the reserved vertices
 * @uv_swap:    the surface swaps U and V
 * @clockwise:  the frame is mirrored: emit with the winding reversed to
 *              keep faces pointing out
 */
typedef struct pcg_target {
    struct mesh *mesh;
    bool        uv_swap;
    bool        clockwise;
} pcg_target;
cres_ret(pcg_target);

/**
 * pcg_sink_reserve() - make room for a surface's vertices
 * @ctx:    the context: sink, palette and chunk
 * @surf:   the surface the vertices belong to
 * @uv:     what the emitted texture coordinates mean
 * @nr_vx:  number of vertices about to be emitted
 *
 * For the shape layer: resolve @surf to its material (the explicit one,
 * or the palette's entry for its role), find or make the bucket of
 * (@ctx->chunk, material, collide class) and guarantee room for @nr_vx
 * more vertices in its current mesh. A reservation that would take the
 * mesh past USHRT_MAX vertices (16-bit indices) starts a new mesh in the
 * bucket instead. Every bucket mesh carries tangents from the start, and
 * the room grows geometrically.
 *
 * Emit into @target.mesh with the _prim_emit_*() writers, which don't
 * allocate. An emitter that reserves for itself, like
 * prim_emit_extrusion(), sizes the mesh to exactly what it emits, so
 * emit everything reserved so far before calling one, and don't hold on
 * to a target across reservations: a later one may hand out another
 * mesh.
 *
 * Return: the target; CERR_INVALID_ARGUMENTS for a skipped or unresolved
 * surface, a material whose UV mode isn't @uv, a bad role or collide
 * class or no vertices; CERR_TOO_LARGE if @nr_vx alone exceeds USHRT_MAX;
 * CERR_BUFFER_OVERRUN if the mesh holds more vertices than were reserved
 * in it; CERR_NOMEM
 */
cres_check(pcg_target) pcg_sink_reserve(const pcg_ctx *ctx, const pcg_surface *surf, pcg_uv_mode uv,
                                        size_t nr_vx);

/**
 * pcg_sink_label() - record a text label
 * @ctx:    the context
 * @surf:   the plate's surface; its material takes PCG_UV_UNIT coordinates
 * @at:     the label in @ctx's frame: its center, with the text reading
 *          along +a and facing +c
 * @h:      plate height
 * @text:   the text, copied
 *
 * Labels are a side channel: the step that turns a sink into models
 * rasterizes them.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a skipped or unresolved
 * surface, a material that doesn't take PCG_UV_UNIT coordinates, a
 * height that isn't positive or no text; CERR_NOMEM
 */
cerr_check pcg_sink_label(const pcg_ctx *ctx, const pcg_surface *surf, const pcg_frame *at, float h,
                          const char *text);

/**
 * struct pcg_key - what the meshes of a bucket share
 * @mat:        the resolved material
 * @chunk:      the batching partition
 * @collide:    &enum pcg_collide
 */
typedef struct pcg_key {
    const pcg_material  *mat;
    unsigned int        chunk;
    uint8_t             collide;
} pcg_key;

/**
 * typedef pcg_sink_mesh_fn - pcg_sink_for_each_mesh() callback
 * @key:    the mesh's bucket
 * @mesh:   the mesh, named "<sink>/<material>/<collide>/<chunk>.<n>"
 *          for the n-th mesh of the bucket; the sink keeps the name and
 *          its own reference to the mesh
 * @priv:   the caller's data
 *
 * Return: CERR_OK to go on; an error stops the walk and is returned
 */
typedef cerr (*pcg_sink_mesh_fn)(const pcg_key *key, struct mesh *mesh, void *priv);

/**
 * pcg_sink_for_each_mesh() - visit every mesh that has vertices
 * @s:      the sink
 * @fn:     the callback
 * @priv:   passed to @fn
 *
 * Buckets come in the order they were made, the meshes of each bucket in
 * the order they filled up.
 *
 * Return: CERR_OK, the first error @fn returned, or CERR_INVALID_ARGUMENTS
 */
cerr_check pcg_sink_for_each_mesh(pcg_sink *s, pcg_sink_mesh_fn fn, void *priv);

/**
 * struct pcg_label - a recorded label
 * @frame:      the label in sink space, see pcg_sink_label()
 * @mat:        the plate's resolved material
 * @text:       the text, owned by the sink
 * @h:          plate height
 * @chunk:      the batching partition it was recorded in
 * @collide:    the plate's &enum pcg_collide
 */
typedef struct pcg_label {
    pcg_frame           frame;
    const pcg_material  *mat;
    const char          *text;
    float               h;
    unsigned int        chunk;
    uint8_t             collide;
} pcg_label;

/**
 * typedef pcg_sink_label_fn - pcg_sink_for_each_label() callback
 * @label:  the label
 * @priv:   the caller's data
 *
 * Return: CERR_OK to go on; an error stops the walk and is returned
 */
typedef cerr (*pcg_sink_label_fn)(const pcg_label *label, void *priv);

/**
 * pcg_sink_for_each_label() - visit every label, in recording order
 * @s:      the sink
 * @fn:     the callback
 * @priv:   passed to @fn
 *
 * Return: CERR_OK, the first error @fn returned, or CERR_INVALID_ARGUMENTS
 */
cerr_check pcg_sink_for_each_label(pcg_sink *s, pcg_sink_label_fn fn, void *priv);

/**
 * struct pcg_sink_stats - what a sink would cost to draw
 * @nr_models:      the models pcg_sink_commit() makes: one per mesh with
 *                  vertices and one per label atlas, that is per chunk,
 *                  material and collide class the labels share
 * @nr_vertices:    vertices in them, 6 per label
 * @nr_casters:     those of the models whose material casts shadows
 * @nr_labels:      recorded labels
 */
typedef struct pcg_sink_stats {
    size_t  nr_models;
    size_t  nr_vertices;
    size_t  nr_casters;
    size_t  nr_labels;
} pcg_sink_stats;

/**
 * pcg_sink_get_stats() - count a sink's models, vertices and casters
 * @s:  the sink
 *
 * Return: the counts, all zero for a nullptr @s
 */
pcg_sink_stats pcg_sink_get_stats(const pcg_sink *s);

#endif /* __CLAP_PCG_SINK_H__ */
