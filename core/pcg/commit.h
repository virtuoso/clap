/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_COMMIT_H__
#define __CLAP_PCG_COMMIT_H__

#include "error.h"
#include "linmath.h"
#include "pcg/material.h"
#include "pcg/sink.h"

struct font;
struct scene;

/**
 * struct pcg_commit_opts - where a sink's geometry goes
 * @scene:  the scene its models and entities join
 * @lib:    material library; the label atlases and the materials that
 *          wear them are made in it
 * @font:   font the labels are lettered in, borrowed; needed only when
 *          the sink has labels
 * @origin: where every entity is placed: the sink's geometry is
 *          relative to it
 * @lod:    LOD every entity is pinned to; -1 leaves it to the distance.
 *          Pinned to 0 by default: distance LODs simplify a merged mesh
 *          as a whole and eat its small parts first.
 */
typedef struct pcg_commit_opts {
    struct scene    *scene;
    pcg_matlib      *lib;
    struct font     *font;
    vec3            origin;
    int             lod;
} pcg_commit_opts;

/**
 * pcg_sink_commit() - turn a sink into models and entities
 * @_s:     the sink
 * @args:   &struct pcg_commit_opts fields
 *
 * Every mesh of the sink becomes a model wearing its bucket's material,
 * with one entity at @origin, translated only: static trimesh colliders
 * ignore rotation, so the geometry already carries it. Entities of solid
 * and hazard surfaces get a static trimesh collider, hazards also the
 * navmesh hazard flag. Labels are lettered into one atlas per chunk,
 * material and collide class, worn by a material derived from theirs,
 * and become quads over their atlas cells: a label reads along its
 * frame's +a and faces +c, and stays readable from the front under a
 * mirrored frame. Blended models join the scene after all the others,
 * because models draw in the order they were added.
 *
 * Every model is made before any of them joins the scene, so a failure
 * to make one leaves the scene alone. A failure after that, making an
 * entity or a collider, leaves what joined so far in the scene.
 *
 * Context: once per sink; the sink's meshes are optimized for drawing,
 * so free the sink afterwards
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing sink, scene or
 * library, a @lod below -1, labels without a @font, or a material
 * without an albedo; CERR_INITIALIZATION_FAILED for a collider that
 * didn't take; the model, texture and physics errors
 */
#define pcg_sink_commit(_s, args...) _pcg_sink_commit((_s), &(const pcg_commit_opts){ args })
cerr_check _pcg_sink_commit(pcg_sink *s, const pcg_commit_opts *opts);

#endif /* __CLAP_PCG_COMMIT_H__ */
