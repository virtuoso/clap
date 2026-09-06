// SPDX-License-Identifier: Apache-2.0
/*
 * Ear clipping triangulation of polygons with holes, after the fashion of
 * mapbox' earcut: every hole is connected to its enclosing ring with a
 * doubled "bridge" edge and the resulting single ring is clipped one ear
 * at a time.
 */
#include <float.h>
#include <math.h>
#include <sys/types.h>
#include "memory.h"
#include "triangulate.h"

/* cross products this small are considered degenerate */
#define TRI_EPS 1e-12

/* twice the signed area of the (a, b, c) triangle; positive when counterclockwise */
static inline double cross2(const float *a, const float *b, const float *c)
{
    return ((double)b[0] - a[0]) * ((double)c[1] - a[1]) -
           ((double)c[0] - a[0]) * ((double)b[1] - a[1]);
}

/* @p inside the counterclockwise (a, b, c) triangle, boundary inclusive */
static bool point_in_triangle(const float *a, const float *b, const float *c, const float *p)
{
    return cross2(a, b, p) >= -TRI_EPS &&
           cross2(b, c, p) >= -TRI_EPS &&
           cross2(c, a, p) >= -TRI_EPS;
}

/* even-odd (crossing number) point in ring test */
static bool point_in_ring(const vec2 *points, const poly_ring *ring, const float *p)
{
    bool inside = false;

    for (size_t i = 0, j = ring->nr - 1; i < ring->nr; j = i++) {
        const float *a = points[ring->start + i];
        const float *b = points[ring->start + j];

        if ((a[1] > p[1]) == (b[1] > p[1]))                                                         continue;
        if (p[0] < a[0] + ((double)p[1] - a[1]) * ((double)b[0] - a[0]) / ((double)b[1] - a[1]))    inside = !inside;
    }

    return inside;
}

float poly_ring_area(const vec2 *points, const poly_ring *ring)
{
    if (!points || !ring || ring->nr < 3)   return 0.0f;

    double area2 = 0.0;
    for (size_t i = 0, j = ring->nr - 1; i < ring->nr; j = i++) {
        const float *a = points[ring->start + j];
        const float *b = points[ring->start + i];

        area2 += (double)a[0] * b[1] - (double)b[0] * a[1];
    }

    return area2 / 2.0;
}

/* number of rings that enclose @ring's first point */
static unsigned int ring_containment_depth(const vec2 *points, const poly_ring *rings,
                                           size_t nr_rings, size_t ring)
{
    unsigned int depth = 0;

    for (size_t j = 0; j < nr_rings; j++) {
        if (j == ring || rings[j].nr < 3)                                   continue;
        if (point_in_ring(points, &rings[j], points[rings[ring].start]))    depth++;
    }

    return depth;
}

cerr poly_rings_classify(const vec2 *points, const poly_ring *rings, size_t nr_rings, bool *hole)
{
    if (!points || !rings || !nr_rings || !hole)    return CERR_INVALID_ARGUMENTS;

    for (size_t i = 0; i < nr_rings; i++)
        hole[i] = rings[i].nr >= 3 && (ring_containment_depth(points, rings, nr_rings, i) & 1);

    return CERR_OK;
}

/* index of the leftmost (lowest on a tie) vertex within @ring */
static size_t ring_leftmost(const vec2 *points, const poly_ring *ring)
{
    size_t best = 0;

    for (size_t i = 1; i < ring->nr; i++) {
        const float *p = points[ring->start + i];
        const float *b = points[ring->start + best];

        if (p[0] < b[0] || (p[0] == b[0] && p[1] < b[1]))   best = i;
    }

    return best;
}

/* copy @ring into @poly with counterclockwise winding */
static size_t poly_fill(unsigned int *poly, const vec2 *points, const poly_ring *ring)
{
    bool ccw = poly_ring_area(points, ring) > 0.0f;

    for (size_t i = 0; i < ring->nr; i++)   poly[i] = ring->start + (ccw ? i : ring->nr - 1 - i);

    return ring->nr;
}

/*
 * Find a vertex of @poly that a hole can be bridged to from the hole's
 * leftmost vertex @m: the closer endpoint of the edge that a leftward ray
 * from @m hits first, adjusted for vertices encroaching on the bridge
 * triangle (David Eberly's method, as simplified by mapbox' earcut)
 */
static ssize_t poly_find_bridge(const vec2 *points, const unsigned int *poly, size_t nr_poly,
                                const float *m)
{
    double qx = -DBL_MAX;
    ssize_t best = -1;

    for (size_t i = 0; i < nr_poly; i++) {
        const float *a = points[poly[i]];
        const float *b = points[poly[(i + 1) % nr_poly]];

        if (a[1] < m[1] || b[1] > m[1] || a[1] == b[1])     continue;

        double x = a[0] + ((double)m[1] - a[1]) * ((double)b[0] - a[0]) / ((double)b[1] - a[1]);
        if (x > m[0] || x <= qx)    continue;

        qx = x;
        best = a[0] < b[0] ? (ssize_t)i : (ssize_t)((i + 1) % nr_poly);
        if (x == m[0])  return best;    /* the ray goes right through a vertex */
    }

    if (best < 0)   return best;

    /*
     * Vertices inside the (@m, ray intersection, bridge candidate) triangle
     * obstruct the bridge; among those, prefer the one closest to the ray,
     * angle-wise
     */
    vec2 cand = { points[poly[best]][0], points[poly[best]][1] };
    vec2 ta = { m[1] < cand[1] ? m[0] : (float)qx, m[1] };
    vec2 tc = { m[1] < cand[1] ? (float)qx : m[0], m[1] };
    double tan_min = DBL_MAX;

    for (size_t i = 0; i < nr_poly; i++) {
        const float *p = points[poly[i]];

        if (p[0] > m[0] || p[0] < cand[0] || p[0] == m[0])  continue;
        if (!point_in_triangle(ta, cand, tc, p))            continue;

        double tan = fabs((double)m[1] - p[1]) / ((double)m[0] - p[0]);
        if (tan < tan_min || (tan == tan_min && p[0] > points[poly[best]][0])) {
            best = i;
            tan_min = tan;
        }
    }

    return best;
}

/*
 * Connect a hole to the enclosing ring: insert the hole's cycle, starting
 * and ending at its leftmost vertex @leftmost, after @bridge, followed by
 * a return to the bridge vertex (a doubled edge)
 */
static size_t poly_splice_hole(unsigned int *poly, size_t nr_poly, size_t bridge,
                               const poly_ring *hole, size_t leftmost, bool stored_ccw)
{
    ssize_t nr = hole->nr;
    ssize_t dir = stored_ccw ? -1 : 1;      /* holes are traversed clockwise */

    memmove(&poly[bridge + 1 + nr + 2], &poly[bridge + 1],
            (nr_poly - bridge - 1) * sizeof(*poly));

    size_t w = bridge + 1;
    for (ssize_t k = 0; k < nr; k++) {
        ssize_t p = ((ssize_t)leftmost + dir * k) % nr;
        if (p < 0)  p += nr;
        poly[w++] = hole->start + p;
    }
    poly[w++] = hole->start + leftmost;
    poly[w++] = poly[bridge];

    return nr_poly + nr + 2;
}

static void poly_remove(unsigned int *poly, size_t *nr_poly, size_t i)
{
    memmove(&poly[i], &poly[i + 1], (*nr_poly - i - 1) * sizeof(*poly));
    (*nr_poly)--;
}

static inline void emit_tri(unsigned int *idx, size_t *nr_tris,
                            unsigned int a, unsigned int b, unsigned int c)
{
    idx[*nr_tris * 3 + 0] = a;
    idx[*nr_tris * 3 + 1] = b;
    idx[*nr_tris * 3 + 2] = c;
    (*nr_tris)++;
}

/**
 * poly_earcut() - triangulate a ring by clipping its ears
 * @points:     points array the ring indexes
 * @poly:       the ring, counterclockwise, holes bridged in; consumed
 * @nr_poly:    number of vertices in @poly
 * @idx:        output triangle indices, 3 per triangle
 *
 * Clip ears off @poly until it's exhausted, emitting the triangles into
 * @idx.
 * Return: the number of triangles emitted
 */
static size_t poly_earcut(const vec2 *points, unsigned int *poly, size_t nr_poly,
                          unsigned int *idx)
{
    size_t nr_tris = 0;
    size_t i = 0, since_clip = 0;

    while (nr_poly > 3) {
        size_t prev = (i + nr_poly - 1) % nr_poly;
        size_t next = (i + 1) % nr_poly;
        unsigned int pa = poly[prev], pb = poly[i], pc = poly[next];

        /* degenerate leftovers of clipped-out bridges */
        if (pa == pb || pb == pc) {
            poly_remove(poly, &nr_poly, i);
            goto removed;
        }

        if (pa == pc) {
            /* a spike folding back on itself: drop both vertices */
            poly_remove(poly, &nr_poly, i);
            poly_remove(poly, &nr_poly, next > i ? next - 1 : next);
            goto removed;
        }

        double cr = cross2(points[pa], points[pb], points[pc]);

        if (fabs(cr) <= TRI_EPS) {
            /* collinear: the middle vertex contributes nothing */
            poly_remove(poly, &nr_poly, i);
            goto removed;
        }

        bool ear = cr > 0.0;
        if (ear)
            /* no other vertex may fall inside a candidate ear */
            for (size_t j = 0; j < nr_poly; j++) {
                if (j == prev || j == i || j == next)   continue;

                unsigned int pj = poly[j];
                if (pj == pa || pj == pb || pj == pc)   continue;
                if (point_in_triangle(points[pa], points[pb], points[pc], points[pj])) {
                    ear = false;
                    break;
                }
            }

        if (ear) {
            emit_tri(idx, &nr_tris, pa, pb, pc);
            poly_remove(poly, &nr_poly, i);
            goto removed;
        }

        i = next;
        if (++since_clip <= nr_poly)    continue;

        /*
         * A full lap without finding an ear: numerical trouble. Clip the
         * first convex vertex regardless of encroachers to guarantee
         * forward progress.
         */
        bool found = false;
        for (size_t k = 0; k < nr_poly; k++) {
            prev = (k + nr_poly - 1) % nr_poly;
            next = (k + 1) % nr_poly;

            if (cross2(points[poly[prev]], points[poly[k]], points[poly[next]]) > TRI_EPS) {
                emit_tri(idx, &nr_tris, poly[prev], poly[k], poly[next]);
                poly_remove(poly, &nr_poly, k);
                found = true;
                break;
            }
        }

        if (!found)     break;      /* fully degenerate remainder */
        i = 0;
        since_clip = 0;
        continue;

removed:
        if (i >= nr_poly)   i = 0;
        since_clip = 0;
    }

    if (nr_poly == 3) {
        unsigned int pa = poly[0], pb = poly[1], pc = poly[2];

        if (pa != pb && pb != pc && pa != pc &&
            cross2(points[pa], points[pb], points[pc]) > TRI_EPS)
            emit_tri(idx, &nr_tris, pa, pb, pc);
    }

    return nr_tris;
}

/**
 * struct ring_info - per-ring scratch state for poly_triangulate()
 * @leftmost:   index of the leftmost vertex within the ring
 * @parent:     enclosing fill ring for holes, -1 otherwise
 * @area:       signed area
 * @hole:       the ring bounds a hole
 */
struct ring_info {
    size_t  leftmost;
    ssize_t parent;
    float   area;
    bool    hole;
};

cres(poly_tris) poly_triangulate(const vec2 *points, const poly_ring *rings, size_t nr_rings)
{
    if (!points || !rings || !nr_rings) return cres_error(poly_tris, CERR_INVALID_ARGUMENTS);

    struct ring_info *info = mem_alloc(sizeof(*info), .nr = nr_rings, .zero = 1);
    if (!info)  return cres_error(poly_tris, CERR_NOMEM);

    size_t total = 0, nr_holes = 0;
    for (size_t i = 0; i < nr_rings; i++) {
        info[i].parent = -1;
        if (rings[i].nr < 3)    continue;

        info[i].area     = poly_ring_area(points, &rings[i]);
        info[i].leftmost = ring_leftmost(points, &rings[i]);
        info[i].hole     = ring_containment_depth(points, rings, nr_rings, i) & 1;
        total += rings[i].nr;
        if (info[i].hole)   nr_holes++;
    }

    poly_tris res = {};
    if (!total) {
        mem_free(info);
        return cres_val(poly_tris, res);
    }

    /* assign each hole to its innermost enclosing fill ring */
    for (size_t i = 0; i < nr_rings; i++) {
        if (!info[i].hole)  continue;

        for (size_t j = 0; j < nr_rings; j++) {
            if (j == i || info[j].hole || rings[j].nr < 3)                                      continue;
            if (!point_in_ring(points, &rings[j], points[rings[i].start]))                      continue;
            if (info[i].parent < 0 || fabsf(info[j].area) < fabsf(info[info[i].parent].area))   info[i].parent = j;
        }
    }

    /* every hole is bridged in with 2 extra vertices */
    size_t nr_max = total + nr_holes * 2;

    unsigned int *poly = mem_alloc(sizeof(*poly), .nr = nr_max);
    if (!poly)  goto err_info;

    unsigned int *idx = mem_alloc(sizeof(*idx), .nr = nr_max * 3);
    if (!idx)   goto err_poly;

    for (size_t o = 0; o < nr_rings; o++) {
        if (info[o].hole || rings[o].nr < 3)    continue;

        size_t nr_poly = poly_fill(poly, points, &rings[o]);

        /* bridge the holes in, leftmost first, so that earlier bridges don't obscure later ones */
        for (;;) {
            ssize_t h = -1;

            for (size_t i = 0; i < nr_rings; i++) {
                if (info[i].parent != (ssize_t)o)   continue;
                if (h < 0 || points[rings[i].start + info[i].leftmost][0] <
                             points[rings[h].start + info[h].leftmost][0])
                    h = i;
            }

            if (h < 0)  break;
            info[h].parent = -2;    /* done with this one */

            const float *m = points[rings[h].start + info[h].leftmost];
            ssize_t bridge = poly_find_bridge(points, poly, nr_poly, m);
            if (bridge < 0)     continue;   /* not actually visible: drop the hole */

            nr_poly = poly_splice_hole(poly, nr_poly, bridge, &rings[h],
                                       info[h].leftmost, info[h].area > 0.0f);
        }

        res.nr_tris += poly_earcut(points, poly, nr_poly, &idx[res.nr_tris * 3]);
    }

    res.idx = idx;
    mem_free(poly);
    mem_free(info);

    return cres_val(poly_tris, res);

err_poly:
    mem_free(poly);
err_info:
    mem_free(info);
    return cres_error(poly_tris, CERR_NOMEM);
}
