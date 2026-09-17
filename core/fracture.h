/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_FRACTURE_H__
#define __CLAP_FRACTURE_H__

#include "error.h"
#include "linmath.h"
#include "mesh.h"
#include "prng.h"

/*
 * Voronoi fracturing of triangle meshes: seed points scattered over a mesh
 * cut it into convex cells, one fragment per cell, the cut faces capped so
 * a closed source yields closed fragments. Two uses: pre-fracturing props
 * into fragments that become rigid bodies (see debris.h), and ruining a
 * model by dropping some fragments and merging the rest back into one
 * mesh (fracture_merge()).
 */

/**
 * struct fracture_params - fracture_mesh() parameters
 * @nr_pieces:      number of cells to scatter over the source AABB when
 *                  @seeds is nullptr
 * @seeds:          explicit cell seed points in the source mesh frame,
 *                  one cell each; used instead of random ones
 * @nr_seeds:       number of @seeds
 * @cap_uv_scale:   texture tiles per world unit on the cut faces, which
 *                  are UV mapped in the cut plane; 0 defaults to 1.0
 */
typedef struct fracture_params {
    const vec3      *seeds;
    size_t          nr_seeds;
    unsigned int    nr_pieces;
    float           cap_uv_scale;
} fracture_params;

/**
 * struct fracture_fragment - one fragment of a fractured mesh
 * @mesh:       the fragment's geometry: a triangle soup carrying the
 *              source's attributes, vertices relative to @center
 * @neighbors:  per triangle of @mesh as produced: the index of the
 *              fragment across the cut face the triangle belongs to,
 *              -1 on the source's own faces
 * @center:     the fragment's volume centroid in the source mesh frame
 * @volume:     the fragment's volume, 0 for an open (sheet) fragment
 */
typedef struct fracture_fragment {
    struct mesh     *mesh;
    int             *neighbors;
    vec3            center;
    float           volume;
} fracture_fragment;

/**
 * struct fracture_fragments - fracture_mesh() output
 * @fragments:  the fragments; cells that miss the mesh yield none
 * @nr:         number of fragments
 */
typedef struct fracture_fragments {
    fracture_fragment   *fragments;
    size_t              nr;
} fracture_fragments;

cres_ret(fracture_fragments);

/**
 * fracture_mesh() - cut a mesh into Voronoi cell fragments
 * @src:        source mesh: vec3 positions, 16-bit indices, optional
 *              UVs, normals and tangents; skinning attributes are not
 *              supported
 * @params:     fracturing parameters
 * @rng:        random stream for the seed points; may be nullptr if
 *              @params->seeds is given
 *
 * Each cell is the intersection of the half-spaces bounded by the
 * bisector planes between its seed and every other seed; the mesh is
 * clipped into each cell, the attributes interpolated along the cut
 * edges, and every closed cut loop is capped with a face lying in the
 * cut plane, so a closed source yields closed fragments. Open sources
 * fracture into open sheets. Fragments are named after @src and left
 * unwelded (see mesh_optimize()).
 *
 * Return: the fragments to be fracture_fragments_release()d by the
 * caller, CERR_INVALID_ARGUMENTS on a bad layout or no way to get
 * seeds, CERR_TOO_LARGE if a fragment can't fit 16-bit indices, or
 * CERR_NOMEM.
 */
cres_check(fracture_fragments) fracture_mesh(struct mesh *src, const fracture_params *params,
                                             prng *rng);

/**
 * fracture_fragments_release() - drop fracture_mesh() output
 * @ff:     the fragments
 *
 * Releases the fragment meshes (see fracture_fragment) and the array.
 */
void fracture_fragments_release(fracture_fragments *ff);

/**
 * fracture_merge() - merge fragments back into one mesh
 * @ff:     the fragments
 * @keep:   per-fragment flags: which ones make it into the result; nullptr
 *          keeps all of them
 * @name:   name of the resulting mesh
 *
 * Put the kept fragments back into the source mesh frame in one mesh:
 * a ruined version of the source when some fragments are dropped. The
 * cut faces towards dropped fragments stay, those between two kept
 * fragments are hidden inside and are left out. The fragments must be
 * as fracture_mesh() produced them (see fracture_fragment::neighbors).
 *
 * Return: the new mesh, CERR_INVALID_ARGUMENTS if nothing is kept,
 * CERR_TOO_LARGE if it can't fit 16-bit indices, or CERR_NOMEM.
 */
cresp_check(mesh) fracture_merge(const fracture_fragments *ff, const bool *keep,
                                 const char *name);

#endif /* __CLAP_FRACTURE_H__ */
