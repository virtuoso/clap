/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_TRIANGULATE_H__
#define __CLAP_TRIANGULATE_H__

#include "error.h"
#include "linmath.h"

/**
 * struct poly_ring - one closed ring of a multi-ring polygon
 * @start:  index of the ring's first point in the shared points array
 * @nr:     number of points in the ring
 *
 * Rings are implicitly closed: the last point connects back to the first
 * one. Winding order is arbitrary: fill boundaries and holes are told
 * apart by containment (see poly_rings_classify()).
 */
typedef struct poly_ring {
    size_t  start;
    size_t  nr;
} poly_ring;

/**
 * struct poly_tris - triangulation output
 * @idx:        vertex indices into the source points array, 3 per triangle
 * @nr_tris:    number of triangles
 */
typedef struct poly_tris {
    unsigned int    *idx;
    size_t          nr_tris;
} poly_tris;

cres_ret(poly_tris);

/**
 * poly_ring_area() - signed area of a polygon ring
 * @points: points array shared between the rings
 * @ring:   the ring
 *
 * Return: the ring's signed area: positive for counterclockwise winding
 * (X right, Y up), zero for degenerate rings.
 */
float poly_ring_area(const vec2 *points, const poly_ring *ring);

/**
 * poly_rings_classify() - tell fill boundary rings from hole rings
 * @points:     points array shared between the rings
 * @rings:      rings of a polygon
 * @nr_rings:   number of rings
 * @hole:       output array of @nr_rings elements
 *
 * A ring enclosed by an odd number of other rings bounds a hole; @hole
 * elements are set accordingly. Winding order doesn't matter.
 * Return: CERR_OK or CERR_INVALID_ARGUMENTS.
 */
cerr_check poly_rings_classify(const vec2 *points, const poly_ring *rings, size_t nr_rings,
                               bool *hole);

/**
 * poly_triangulate() - triangulate a polygon with holes
 * @points:     points array shared between the rings
 * @rings:      rings of a polygon: fill boundaries and holes in any order
 * @nr_rings:   number of rings
 *
 * Ear clipping triangulation of a non-self-intersecting multi-ring
 * polygon: holes are bridged into their enclosing rings mapbox-earcut
 * style and the resulting rings are clipped one ear at a time. Winding
 * order of the input rings is arbitrary; output triangles are wound
 * counterclockwise. Degenerate rings (fewer than 3 points) are skipped.
 *
 * Return: poly_tris with poly_tris::idx to be mem_free()d by the caller,
 * or CERR_NOMEM / CERR_INVALID_ARGUMENTS.
 */
cres_check(poly_tris) poly_triangulate(const vec2 *points, const poly_ring *rings,
                                       size_t nr_rings);

#endif /* __CLAP_TRIANGULATE_H__ */
