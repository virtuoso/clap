/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_POLY2D_H__
#define __CLAP_POLY2D_H__

#include "error.h"
#include "linmath.h"

/*
 * Polygons in a plane: vec2 point arrays, implicitly closed (the last
 * point connects back to the first one). The helpers don't care which
 * plane; 3D callers use (x, z), the ground plane. "Counterclockwise" and
 * "left" are in the math orientation of the pair: the first coordinate
 * to the right, the second one up.
 */

/**
 * struct poly2d_halfplane - the points p with dot(p, n) <= d
 * @n:  the outward normal of the boundary line
 * @d:  the boundary line's offset along @n, times |@n|
 *
 * @n needn't be unit length; the tolerances of poly2d_segment_clip() are
 * in units of dot(p, @n), so they are distances when it is.
 */
typedef struct poly2d_halfplane {
    vec2    n;
    float   d;
} poly2d_halfplane;

/**
 * poly2d_regular_vertex() - one vertex of a regular polygon
 * @out:    the vertex
 * @j:      vertex index; any int, taken mod @n by the angle
 * @n:      number of sides, at least 3
 * @r:      circumradius
 * @phase:  angle of vertex 0, in radians from the first axis toward the second
 *
 * Vertex @j sits at angle @phase + @j * 2pi / @n: vertices run counter-
 * clockwise with increasing @j, and a negative @j walks the ring the other
 * way (a hole ring wound against its outer ring).
 */
void poly2d_regular_vertex(vec2 out, int j, int n, float r, float phase);

/**
 * poly2d_regular() - a regular polygon
 * @out:    @n vertices
 * @n:      number of sides, at least 3
 * @r:      circumradius
 * @phase:  angle of vertex 0 (see poly2d_regular_vertex())
 *
 * Fills @out with vertices 0 to @n - 1 of poly2d_regular_vertex(), wound
 * counterclockwise.
 */
void poly2d_regular(vec2 *out, int n, float r, float phase);

/**
 * poly2d_phase_edge0_neg_z() - the phase that turns edge 0 to face -z
 * @n:  number of sides, at least 3
 *
 * With this phase, edge 0 (vertex 0 to vertex 1) of a regular @n-gon in
 * (x, z) is perpendicular to the z axis on its negative side: its midpoint
 * is (0, -r * cos(pi / @n)), and the sides count counterclockwise from
 * there.
 *
 * Return: the phase for poly2d_regular() and poly2d_regular_vertex()
 */
float poly2d_phase_edge0_neg_z(int n);

/**
 * poly2d_bbox() - bounding box of a point set
 * @pts:    points
 * @n:      number of points
 * @lo:     minimum of each coordinate
 * @hi:     maximum of each coordinate
 *
 * The box is seeded from @pts[0], so it fits polygons anywhere in the
 * plane, not only ones around the origin. An empty set gives
 * @lo = @hi = (0, 0).
 */
void poly2d_bbox(const vec2 *pts, size_t n, vec2 lo, vec2 hi);

/**
 * poly2d_clip() - clip a polygon against a half-plane
 * @in:     points of the polygon
 * @n:      number of points in @in
 * @hp:     the half-plane to keep
 * @out:    the clipped polygon; must not overlap @in
 * @cap:    capacity of @out in points
 *
 * One Sutherland-Hodgman step: keeps the part of @in inside @hp, with the
 * winding of @in. Points on the boundary count as inside. A convex
 * polygon of @n points clips to at most @n + 1 points, any polygon to at
 * most 2 * @n. Clipping against several half-planes is one call per
 * half-plane, ping-ponging between two buffers.
 *
 * Fewer than 3 points in @out means nothing with an area is left, but 3
 * or more don't promise an area. A polygon that only touches the boundary
 * keeps its points on it, and a point on the boundary followed by an
 * outside one comes out twice, so the result can be 3 or more points
 * with zero area. A caller that needs an area measures it.
 *
 * Return: the number of points in @out; CERR_TOO_LARGE if they don't fit
 * in @cap points; CERR_INVALID_ARGUMENTS for a missing @hp, @in or @out.
 */
cres_check(size_t) poly2d_clip(const vec2 *in, size_t n, const poly2d_halfplane *hp, vec2 *out, size_t cap);

/**
 * poly2d_chord() - a polygon's extent along a line
 * @pts:    points of the polygon
 * @n:      number of points
 * @org:    origin of the line's frame
 * @dir:    unit direction of the frame's first axis, along the line
 * @off:    the line's offset along the frame's second axis, which is @dir
 *          turned left: (-@dir[1], @dir[0])
 * @lo:     if not nullptr, the lowest crossing along @dir, measured from @org
 * @hi:     if not nullptr, the highest crossing along @dir, measured from @org
 *
 * Crosses every edge of the polygon with the line and reports the extent
 * of the crossings along it: for a convex polygon, the chord. An edge
 * crosses when its ends lie on different sides of the line, a point on the
 * line counting as being on its right side, so a vertex on the line is
 * crossed once. In the frame at the origin with @dir = (1, 0) the line is
 * the second coordinate = @off; with @dir = (0, 1), the first coordinate
 * = -@off.
 *
 * Return: @hi - @lo, the chord's length; 0 when the line misses the
 * polygon, and then @lo and @hi are 0 too
 */
float poly2d_chord(const vec2 *pts, size_t n, const vec2 org, const vec2 dir, float off,
                   float *lo, float *hi);

/**
 * poly2d_segment_clip() - the part of a segment inside a set of half-planes
 * @a:      the segment's start, at parameter 0
 * @b:      the segment's end, at parameter 1
 * @hp:     the half-planes; the segment is clipped to their intersection
 * @nr:     number of half-planes
 * @t0:     the parameter where the kept part starts, written on success
 * @t1:     the parameter where the kept part ends, written on success
 *
 * Point a + t * (b - a) is inside for t in [@t0, @t1], a sub-interval of
 * [0, 1]. A segment parallel to a boundary line (its ends' dot(p, n) - d
 * within 1e-6 of each other) is inside that half-plane entirely or not at
 * all, by its start, so a segment lying on a boundary stays inside. With
 * no half-planes, the whole segment is inside.
 *
 * The length of the kept piece is measured in the parameter, not in
 * space: a degenerate segment (@a = @b) inside every half-plane is kept
 * whole, as [0, 1].
 *
 * Return: true if a piece with @t1 > @t0 is inside, false if the segment
 * misses the intersection or only touches it
 */
bool poly2d_segment_clip(const vec2 a, const vec2 b, const poly2d_halfplane *hp, size_t nr,
                         float *t0, float *t1);

/**
 * poly2d_edge_frame() - the run frame of an edge
 * @a:      the edge's start
 * @b:      the edge's end
 * @dir:    unit direction from @a to @b
 * @left:   @dir turned left, (-@dir[1], @dir[0]): the inside of a
 *          counterclockwise polygon's edge
 *
 * Together with @a as the origin, @dir and @left are the frame to build
 * along the edge in: walls, railings, anything that runs with it. A
 * zero-length edge has no direction: it gives @dir = @left = (0, 0).
 *
 * Return: the edge's length
 */
float poly2d_edge_frame(const vec2 a, const vec2 b, vec2 dir, vec2 left);

#endif /* __CLAP_POLY2D_H__ */
