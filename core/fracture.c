// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "fracture.h"
#include "memory.h"
#include "triangulate.h"
#include "util.h"

/*
 * A cell is cut out of the source by clipping it against the bisector
 * plane of every other seed in turn. The source's triangles are clipped
 * as convex polygons: a clip keeps the part on the seed's side and
 * records the edge where the polygon crossed the plane; after a plane
 * has been applied to all polygons, the crossing edges are chained into
 * loops and the loops are triangulated into cap faces lying in the
 * plane, which then get clipped by the planes that follow like any
 * other polygon. Polygons are only triangulated on the way out.
 *
 * Staying with polygons matters for the fragments' neighbours: two
 * cells cut the same source triangle along the same line, and a convex
 * polygon meets a line in one edge, so both sides get the same edge
 * with the same endpoints. Triangulating as we go would put each cell's
 * own diagonals' crossings on that line, and the cut faces would only
 * match up to T-junctions.
 *
 * Positions are handled in double precision throughout: the same point
 * gets computed along different routes (the two polygons sharing a cut
 * edge, the two cells sharing a cut face), and where an edge runs
 * nearly parallel to a cutting plane the crossing is ill-conditioned.
 */

typedef double dvec3[3];

static void d3_from(dvec3 r, const float *v)
{
    r[0] = v[0]; r[1] = v[1]; r[2] = v[2];
}

static void d3_to(float *r, const dvec3 v)
{
    r[0] = v[0]; r[1] = v[1]; r[2] = v[2];
}

static void d3_dup(dvec3 r, const dvec3 v)
{
    r[0] = v[0]; r[1] = v[1]; r[2] = v[2];
}

static void d3_sub(dvec3 r, const dvec3 a, const dvec3 b)
{
    r[0] = a[0] - b[0]; r[1] = a[1] - b[1]; r[2] = a[2] - b[2];
}

static void d3_add(dvec3 r, const dvec3 a, const dvec3 b)
{
    r[0] = a[0] + b[0]; r[1] = a[1] + b[1]; r[2] = a[2] + b[2];
}

static void d3_scale(dvec3 r, const dvec3 v, double s)
{
    r[0] = v[0] * s; r[1] = v[1] * s; r[2] = v[2] * s;
}

static double d3_dot(const dvec3 a, const dvec3 b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static double d3_len(const dvec3 v)
{
    return sqrt(d3_dot(v, v));
}

static void d3_norm(dvec3 r, const dvec3 v)
{
    d3_scale(r, v, 1.0 / d3_len(v));
}

static void d3_cross(dvec3 r, const dvec3 a, const dvec3 b)
{
    dvec3 c = {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    };

    d3_dup(r, c);
}

/* a clipped vertex: every attribute the source may carry */
struct fvx {
    dvec3   p;
    vec2    t;
    vec3    n;
    vec4    tg;
};

/*
 * A convex polygon: @nr vertices from @start in the cell's vertex buffer.
 * @neighbor: seed index of the cell across a cap face, -1 on source faces
 */
struct fpoly {
    size_t          start;
    unsigned int    nr;
    int             neighbor;
};

/* a cut edge: where a polygon crossed the current cut plane */
struct fseg {
    dvec3   a;
    dvec3   b;
};

/* a cap triangle: three indices into the cut plane's ring points */
typedef unsigned int fcap_tri[3];

/* append one element to a darray by value */
static cerr da_push(struct darray *da, const void *el)
{
    void *slot = _darray_add(da);

    if (!slot)  return CERR_NOMEM;

    memcpy(slot, el, da->elsz);

    return CERR_OK;
}

/* replace @dst's elements with @src's */
static cerr da_copy(struct darray *dst, const struct darray *src)
{
    if (!src->nr_el) {
        _darray_resize(dst, 0);
        return CERR_OK;
    }

    if (!_darray_resize(dst, src->nr_el))   return CERR_NOMEM;

    memcpy(dst->array, src->array, src->nr_el * src->elsz);

    return CERR_OK;
}

struct fcell {
    darray(struct fvx, vxs);        /* the polygons' vertices */
    darray(struct fpoly, polys);
    darray(struct fvx, nvxs);       /* clip output, swapped with the above */
    darray(struct fpoly, npolys);
    darray(struct fseg, segs);
    darray(double, side);           /* per-vertex side of the plane, scratch */
    darray(struct fvx, clip);       /* one clipped polygon, scratch */
};

static void cell_init(struct fcell *c)
{
    darray_init(c->vxs);
    darray_init(c->polys);
    darray_init(c->nvxs);
    darray_init(c->npolys);
    darray_init(c->segs);
    darray_init(c->side);
    darray_init(c->clip);
}

/* what's common to all cells of one fracture_mesh() call */
struct fstate {
    double          eps;        /* weld tolerance for cut loop endpoints */
    double          diag;       /* source AABB diagonal */
    unsigned int    attrs;      /* MESH_*_BIT of the source's optional attributes */
    float           uv_scale;
    bool            clockwise;  /* source winding, mirrored onto the caps */
};

static void fvx_load(struct fvx *v, struct mesh *src, unsigned int i, unsigned int attrs)
{
    d3_from(v->p, &mesh_vx(src)[i * 3]);
    if (attrs & MESH_TX_BIT)        vec2_dup(v->t, &mesh_tx(src)[i * 2]);
    if (attrs & MESH_NORM_BIT)      vec3_dup(v->n, &mesh_norm(src)[i * 3]);
    if (attrs & MESH_TANGENTS_BIT)  vec4_dup(v->tg, &mesh_tangent(src)[i * 4]);
}

static void fvx_store(const struct fvx *v, struct mesh *m, size_t i, unsigned int attrs)
{
    d3_to(&mesh_vx(m)[i * 3], v->p);
    if (attrs & MESH_TX_BIT)        vec2_dup(&mesh_tx(m)[i * 2], v->t);
    if (attrs & MESH_NORM_BIT)      vec3_dup(&mesh_norm(m)[i * 3], v->n);
    if (attrs & MESH_TANGENTS_BIT)  vec4_dup(&mesh_tangent(m)[i * 4], v->tg);
}

static void fvx_lerp(struct fvx *r, const struct fvx *a, const struct fvx *b, double t,
                     unsigned int attrs)
{
    float tf = t;

    for (int i = 0; i < 3; i++) r->p[i] = a->p[i] + (b->p[i] - a->p[i]) * t;

    if (attrs & MESH_TX_BIT)
        for (int i = 0; i < 2; i++) r->t[i] = a->t[i] + (b->t[i] - a->t[i]) * tf;

    if (attrs & MESH_NORM_BIT) {
        for (int i = 0; i < 3; i++) r->n[i] = a->n[i] + (b->n[i] - a->n[i]) * tf;
        vec3_norm_safe(r->n, r->n);
    }

    if (attrs & MESH_TANGENTS_BIT) {
        for (int i = 0; i < 3; i++) r->tg[i] = a->tg[i] + (b->tg[i] - a->tg[i]) * tf;
        vec3_norm_safe(r->tg, r->tg);
        r->tg[3] = a->tg[3];
    }
}

static bool pos_less(const dvec3 a, const dvec3 b)
{
    for (int i = 0; i < 3; i++) {
        if (a[i] < b[i])    return true;
        if (a[i] > b[i])    return false;
    }

    return false;
}

static bool pos_equal(const dvec3 a, const dvec3 b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

/*
 * Where the edge a-b crosses the plane: one end is inside (s <= 0), the
 * other outside (s > 0). Computed from the lexicographically smaller end
 * so both polygons sharing the edge get the same point.
 */
static void edge_cut(struct fvx *out, const struct fvx *a, double sa, const struct fvx *b, double sb,
                     unsigned int attrs)
{
    if (pos_less(b->p, a->p)) {
        const struct fvx *tmp = a;
        double stmp = sa;

        a = b;  sa = sb;
        b = tmp; sb = stmp;
    }

    fvx_lerp(out, a, b, sa / (sa - sb), attrs);
}

/*
 * Append a polygon, repeated positions dropped (clipping at a vertex
 * lying on the plane makes them); nothing is added for a polygon with
 * fewer than three left.
 */
static cerr poly_emit(struct darray *vxs, struct darray *polys, const struct fvx *v,
                      unsigned int nr, int neighbor)
{
    struct fpoly p = { .start = vxs->nr_el, .neighbor = neighbor };

    for (unsigned int k = 0; k < nr; k++) {
        if (p.nr) {
            const struct fvx *first = &((struct fvx *)vxs->array)[p.start];
            const struct fvx *last = &((struct fvx *)vxs->array)[vxs->nr_el - 1];

            if (pos_equal(v[k].p, last->p) || pos_equal(v[k].p, first->p))  continue;
        }

        CERR_RET_CERR(da_push(vxs, &v[k]));
        p.nr++;
    }

    if (p.nr < 3) {
        _darray_resize(vxs, p.start);
        return CERR_OK;
    }

    return da_push(polys, &p);
}

/*
 * Clip one polygon against the plane dot(p, n) = d, keeping the part
 * with dot(p, n) <= d (Sutherland-Hodgman), and record where it crossed
 * the plane: a convex polygon crosses it twice, so one cut edge.
 */
static cerr poly_clip(struct fcell *c, const struct fpoly *p, const dvec3 n, double d,
                      unsigned int attrs)
{
    const struct fvx *v = &c->vxs.x[p->start];
    unsigned int nr = p->nr, nr_in = 0;

    if (!darray_resize(c->side, nr))    return CERR_NOMEM;

    double *s = c->side.x;

    for (unsigned int k = 0; k < nr; k++) {
        s[k] = d3_dot(v[k].p, n) - d;
        if (s[k] <= 0.0)    nr_in++;
    }

    if (nr_in == nr)    return poly_emit(&c->nvxs.da, &c->npolys.da, v, nr, p->neighbor);
    if (!nr_in)         return CERR_OK;

    darray_resize(c->clip, 0);

    struct fvx cut[2];
    unsigned int nr_cut = 0;

    for (unsigned int k = 0; k < nr; k++) {
        unsigned int k1 = (k + 1) % nr;

        if (s[k] <= 0.0)    CERR_RET_CERR(da_push(&c->clip.da, &v[k]));

        if ((s[k] <= 0.0) != (s[k1] <= 0.0)) {
            struct fvx x;

            edge_cut(&x, &v[k], s[k], &v[k1], s[k1], attrs);
            CERR_RET_CERR(da_push(&c->clip.da, &x));

            cut[nr_cut++ % 2] = x;
            if (!(nr_cut % 2) && !pos_equal(cut[0].p, cut[1].p)) {
                struct fseg seg;

                d3_dup(seg.a, cut[0].p);
                d3_dup(seg.b, cut[1].p);
                CERR_RET_CERR(da_push(&c->segs.da, &seg));
            }
        }
    }

    return poly_emit(&c->nvxs.da, &c->npolys.da, c->clip.x, darray_count(c->clip), p->neighbor);
}

/* a right-handed basis (u, v, n) in the cut plane: u x v = n */
static void plane_basis(const dvec3 n, dvec3 u, dvec3 v)
{
    dvec3 axis = { 0.0, 0.0, 0.0 };
    int least = 0;

    for (int i = 1; i < 3; i++)
        if (fabs(n[i]) < fabs(n[least]))    least = i;
    axis[least] = 1.0;

    d3_cross(u, n, axis);
    d3_norm(u, u);
    d3_cross(v, n, u);
}

/* index of @p among the welded points, adding it if it's new */
static unsigned int cap_weld(dvec3 *pts, unsigned int *nr_pts, const dvec3 p, double eps)
{
    for (unsigned int i = 0; i < *nr_pts; i++) {
        dvec3 diff;

        d3_sub(diff, pts[i], p);
        if (d3_len(diff) <= eps)    return i;
    }

    d3_dup(pts[*nr_pts], p);

    return (*nr_pts)++;
}

/* the other end of segment @s from point @p, or -1 if @p isn't on it */
static int seg_other_end(const unsigned int (*ends)[2], unsigned int s, unsigned int p)
{
    if (ends[s][0] == p)    return ends[s][1];
    if (ends[s][1] == p)    return ends[s][0];
    return -1;
}

/* how many cap triangles run the edge a-b, either way */
static unsigned int cap_edge_count(const struct darray *cap, unsigned int a, unsigned int b)
{
    unsigned int count = 0;

    for (size_t t = 0; t < cap->nr_el; t++) {
        const unsigned int *tri = ((const fcap_tri *)cap->array)[t];

        for (int e = 0; e < 3; e++) {
            unsigned int x = tri[e], y = tri[(e + 1) % 3];

            if ((x == a && y == b) || (x == b && y == a))   count++;
        }
    }

    return count;
}

/*
 * The side faces meet every ring point, so a ring edge the cap doesn't
 * have would be a T-junction: the triangulator leaves collinear ring
 * points out, sometimes with a zero-area ear in their place, sometimes
 * with nothing. Either way the cap's outline then has an edge spanning
 * over them; split the triangle on that edge at the first spanned point
 * that is missing a ring edge, until every ring edge is in the cap.
 * Outline edges run along the ring in the triangles' (counterclockwise)
 * winding: @forward says whether that is the ring's own order.
 */
static cerr cap_stitch_ring(struct darray *cap, const poly_ring *ring, bool forward)
{
    size_t start = ring->start, nr = ring->nr;

    for (size_t pass = 0; pass < nr; pass++) {
        bool progress = false, missing = false;

        for (size_t k = 0; k < nr; k++) {
            unsigned int a = start + k, b = start + (k + 1) % nr;

            if (cap_edge_count(cap, a, b))  continue;
            missing = true;

            for (size_t t = 0; t < cap->nr_el && !progress; t++)
                for (int e = 0; e < 3 && !progress; e++) {
                    unsigned int *tri = ((fcap_tri *)cap->array)[t];
                    unsigned int x = tri[e], y = tri[(e + 1) % 3], cc = tri[(e + 2) % 3];

                    if (x < start || x >= start + nr || y < start || y >= start + nr)   continue;

                    /* an outline edge whose arc x..y along the ring covers ring edge k */
                    size_t ix = forward ? x - start : y - start;
                    size_t iy = forward ? y - start : x - start;
                    size_t arc = (iy + nr - ix) % nr;

                    if (arc < 2 || (k + nr - ix) % nr >= arc)   continue;
                    if (cap_edge_count(cap, x, y) != 1)         continue;

                    for (size_t m = 1; m < arc && !progress; m++) {
                        unsigned int p = start + (ix + m) % nr;
                        unsigned int prev = start + (ix + m - 1) % nr;
                        unsigned int next = start + (ix + m + 1) % nr;

                        if (cap_edge_count(cap, prev, p) && cap_edge_count(cap, p, next))   continue;

                        unsigned int split[3] = { p, y, cc };

                        tri[(e + 1) % 3] = p;
                        CERR_RET_CERR(da_push(cap, split));
                        progress = true;
                    }
                }
        }

        if (!missing || !progress)  break;
    }

    return CERR_OK;
}

/*
 * Chain the cut edges into closed loops and emit the faces that close
 * them. Open chains (an open source mesh) are dropped: nothing to cap.
 */
static cerr cell_cap(struct fcell *c, const struct fstate *st, const dvec3 n, double d, int neighbor)
{
    size_t nr_segs = darray_count(c->segs);

    if (nr_segs < 3)    return CERR_OK;

    LOCAL(void, pts_buf);
    LOCAL(void, ends_buf);
    LOCAL(void, used_buf);
    LOCAL(void, ring_pts_buf);
    LOCAL(void, ring_src_buf);
    LOCAL(void, rings_buf);
    LOCAL(void, holes_buf);

    dvec3 *pts = pts_buf = mem_alloc(sizeof(dvec3), .nr = nr_segs * 2);
    unsigned int (*ends)[2] = ends_buf = mem_alloc(sizeof(*ends), .nr = nr_segs);
    bool *used = used_buf = mem_alloc(sizeof(*used), .nr = nr_segs, .zero = 1);
    /*
     * a closed loop of k segments is k ring points, an open chain of k
     * segments k + 1 of them: one more than there are segments at most
     */
    vec2 *ring_pts = ring_pts_buf = mem_alloc(sizeof(vec2), .nr = nr_segs + 1);
    unsigned int *ring_src = ring_src_buf = mem_alloc(sizeof(*ring_src), .nr = nr_segs + 1);
    poly_ring *rings = rings_buf = mem_alloc(sizeof(*rings), .nr = nr_segs);
    bool *holes = holes_buf = mem_alloc(sizeof(*holes), .nr = nr_segs);

    if (!pts || !ends || !used || !ring_pts || !ring_src || !rings || !holes)   return CERR_NOMEM;

    unsigned int nr_pts = 0;

    for (size_t i = 0; i < nr_segs; i++) {
        struct fseg *s = &c->segs.x[i];

        ends[i][0] = cap_weld(pts, &nr_pts, s->a, st->eps);
        ends[i][1] = cap_weld(pts, &nr_pts, s->b, st->eps);
        /* both ends welded to one point: nothing to chain */
        if (ends[i][0] == ends[i][1])   used[i] = true;
    }

    dvec3 u, v;
    plane_basis(n, u, v);

    size_t nr_ring_pts = 0, nr_rings = 0;

    for (size_t i = 0; i < nr_segs; i++) {
        if (used[i])    continue;

        unsigned int start = ends[i][0], cur = ends[i][1];
        size_t ring_start = nr_ring_pts;
        bool closed = false;

        used[i] = true;
        ring_src[nr_ring_pts++] = start;

        for (;;) {
            if (cur == start) {
                closed = true;
                break;
            }

            ring_src[nr_ring_pts++] = cur;

            int next = -1;
            for (size_t j = 0; j < nr_segs && next < 0; j++)
                if (!used[j] && seg_other_end(ends, j, cur) >= 0) {
                    used[j] = true;
                    next = seg_other_end(ends, j, cur);
                }

            if (next < 0)   break;
            cur = next;
        }

        if (!closed || nr_ring_pts - ring_start < 3) {
            nr_ring_pts = ring_start;
            continue;
        }

        rings[nr_rings].start = ring_start;
        rings[nr_rings].nr = nr_ring_pts - ring_start;
        nr_rings++;
    }

    if (!nr_rings)  return CERR_OK;

    for (size_t i = 0; i < nr_ring_pts; i++) {
        ring_pts[i][0] = d3_dot(pts[ring_src[i]], u);
        ring_pts[i][1] = d3_dot(pts[ring_src[i]], v);
    }

    poly_tris tris = CRES_RET_CERR(poly_triangulate(ring_pts, rings, nr_rings));

    LOCAL_SET(void, tris_buf) = tris.idx;
    darray(fcap_tri, cap);
    cerr err = CERR_OK;

    darray_init(cap);

    /*
     * Every triangle stays, the zero-area ears of collinear ring points
     * included: they keep the cap's edges matching the sides' exactly.
     */
    for (size_t i = 0; i < tris.nr_tris && !IS_CERR(err); i++)  err = da_push(&cap.da, &tris.idx[i * 3]);
    if (IS_CERR(err))                                           goto out;

    /* a fill ring's outline runs counterclockwise, a hole's clockwise */
    err = poly_rings_classify(ring_pts, rings, nr_rings, holes);
    for (size_t r = 0; r < nr_rings && !IS_CERR(err); r++) {
        bool ccw = poly_ring_area(ring_pts, &rings[r]) > 0.0f;

        err = cap_stitch_ring(&cap.da, &rings[r], ccw != holes[r]);
    }
    if (IS_CERR(err))   goto out;

    for (size_t i = 0; i < darray_count(cap) && !IS_CERR(err); i++) {
        const unsigned int *tri = cap.x[i];
        struct fvx cv[3];

        /* CCW in (u, v) faces +n, outward; mirror for clockwise sources */
        for (int k = 0; k < 3; k++) {
            unsigned int idx = tri[st->clockwise ? 2 - k : k];
            struct fvx *fv = &cv[k];

            d3_dup(fv->p, pts[ring_src[idx]]);
            fv->t[0] = ring_pts[idx][0] * st->uv_scale;
            fv->t[1] = ring_pts[idx][1] * st->uv_scale;
            d3_to(fv->n, n);
            d3_to(fv->tg, u);
            fv->tg[3] = 1.0f;
        }

        err = poly_emit(&c->nvxs.da, &c->npolys.da, cv, 3, neighbor);
    }

out:
    darray_clearout(cap);

    return err;
}

/* true if the whole cell is already on the inner side of the plane */
static bool cell_inside(struct fcell *c, const dvec3 n, double d)
{
    if (!darray_count(c->vxs))  return true;

    dvec3 lo, hi;

    d3_dup(lo, c->vxs.x[0].p);
    d3_dup(hi, lo);

    for (size_t i = 1; i < darray_count(c->vxs); i++) {
        const double *p = c->vxs.x[i].p;

        for (int j = 0; j < 3; j++) {
            lo[j] = fmin(lo[j], p[j]);
            hi[j] = fmax(hi[j], p[j]);
        }
    }

    double far = -d;
    for (int j = 0; j < 3; j++) far += n[j] * (n[j] > 0.0 ? hi[j] : lo[j]);

    return far <= 0.0;
}

/* clip the cell against the bisector plane between @seed and @other (cell @neighbor) */
static cerr cell_clip(struct fcell *c, const struct fstate *st, const vec3 seed, const vec3 other,
                      int neighbor)
{
    dvec3 s, o, n, mid;

    d3_from(s, seed);
    d3_from(o, other);
    d3_sub(n, o, s);
    if (d3_len(n) == 0.0)   return CERR_OK;
    d3_norm(n, n);
    d3_add(mid, s, o);
    d3_scale(mid, mid, 0.5);

    double d = d3_dot(mid, n);

    if (cell_inside(c, n, d))   return CERR_OK;

    darray_resize(c->nvxs, 0);
    darray_resize(c->npolys, 0);
    darray_resize(c->segs, 0);

    for (size_t i = 0; i < darray_count(c->polys); i++) CERR_RET_CERR(poly_clip(c, &c->polys.x[i], n, d, st->attrs));

    CERR_RET_CERR(cell_cap(c, st, n, d, neighbor));

    struct darray tmp = c->vxs.da;
    c->vxs.da = c->nvxs.da;
    c->nvxs.da = tmp;
    tmp = c->polys.da;
    c->polys.da = c->npolys.da;
    c->npolys.da = tmp;

    return CERR_OK;
}

/*
 * Signed volume and centroid by the divergence theorem over the
 * (closed, consistently wound) surface: tetrahedra from the origin to
 * each polygon's fan triangles. Open surfaces have no volume: the
 * centroid falls back to the AABB center.
 */
static double cell_volume(const struct fcell *c, double diag, dvec3 center)
{
    double vol = 0.0, cx = 0.0, cy = 0.0, cz = 0.0, area_sum = 0.0;
    dvec3 lo, hi, area = { 0.0, 0.0, 0.0 };

    d3_dup(lo, c->vxs.x[0].p);
    d3_dup(hi, lo);

    for (size_t i = 0; i < c->vxs.da.nr_el; i++) {
        const double *p = c->vxs.x[i].p;

        for (int j = 0; j < 3; j++) {
            lo[j] = fmin(lo[j], p[j]);
            hi[j] = fmax(hi[j], p[j]);
        }
    }

    for (size_t i = 0; i < c->polys.da.nr_el; i++) {
        const struct fpoly *p = &c->polys.x[i];
        const struct fvx *v = &c->vxs.x[p->start];

        for (unsigned int k = 1; k + 1 < p->nr; k++) {
            dvec3 cross, e1, e2, n;

            d3_cross(cross, v[k].p, v[k + 1].p);

            double tet = d3_dot(v[0].p, cross) / 6.0;

            d3_sub(e1, v[k].p, v[0].p);
            d3_sub(e2, v[k + 1].p, v[0].p);
            d3_cross(n, e1, e2);
            d3_add(area, area, n);
            area_sum += d3_len(n);

            vol += tet;
            cx += tet * (v[0].p[0] + v[k].p[0] + v[k + 1].p[0]) / 4.0;
            cy += tet * (v[0].p[1] + v[k].p[1] + v[k + 1].p[1]) / 4.0;
            cz += tet * (v[0].p[2] + v[k].p[2] + v[k + 1].p[2]) / 4.0;
        }
    }

    /*
     * A closed surface's vector area sums up to nothing, an open one's
     * doesn't: a sheet's is twice its area along its normal
     */
    if (fabs(vol) <= 1e-9 * diag * diag * diag || d3_len(area) > 1e-6 * area_sum) {
        d3_add(center, lo, hi);
        d3_scale(center, center, 0.5);
        return 0.0;
    }

    center[0] = cx / vol;
    center[1] = cy / vol;
    center[2] = cz / vol;

    return fabs(vol);
}

/* the cell's polygons fan-triangulated into a mesh, per-triangle neighbors alongside */
static cresp(mesh) cell_mesh(const struct fcell *c, const char *name, unsigned int attrs,
                             int **neighbors)
{
    size_t nr_tris = 0;

    for (size_t i = 0; i < c->polys.da.nr_el; i++)  nr_tris += c->polys.x[i].nr - 2;

    size_t nr = nr_tris * 3;

    if (nr > USHRT_MAX)     return cresp_error(mesh, CERR_TOO_LARGE);

    LOCAL(void, nb_buf);
    int *nb = nb_buf = mem_alloc(sizeof(*nb), .nr = nr_tris);

    if (!nb)    return cresp_error(mesh, CERR_NOMEM);

    LOCAL_SET(mesh_t, m) = CRES_RET_T(ref_new_checked(mesh, .name = name), mesh);

    CERR_RET_T(mesh_attr_alloc(m, MESH_VX, sizeof(float) * 3, nr), mesh);
    CERR_RET_T(mesh_attr_alloc(m, MESH_IDX, sizeof(unsigned short), nr), mesh);
    if (attrs & MESH_TX_BIT)        CERR_RET_T(mesh_attr_alloc(m, MESH_TX, sizeof(float) * 2, nr), mesh);
    if (attrs & MESH_NORM_BIT)      CERR_RET_T(mesh_attr_alloc(m, MESH_NORM, sizeof(float) * 3, nr), mesh);
    if (attrs & MESH_TANGENTS_BIT)  CERR_RET_T(mesh_attr_alloc(m, MESH_TANGENTS, sizeof(float) * 4, nr), mesh);

    size_t out = 0, t = 0;

    for (size_t i = 0; i < c->polys.da.nr_el; i++) {
        const struct fpoly *p = &c->polys.x[i];
        const struct fvx *v = &c->vxs.x[p->start];

        for (unsigned int k = 1; k + 1 < p->nr; k++, t++) {
            const struct fvx *tri[3] = { &v[0], &v[k], &v[k + 1] };

            for (int j = 0; j < 3; j++, out++) {
                fvx_store(tri[j], m, out, attrs);
                mesh_idx(m)[out] = out;
            }
            nb[t] = p->neighbor;
        }
    }

    for (enum mesh_attrs a = 0; a < MESH_MAX; a++)
        if (a == MESH_VX || a == MESH_IDX || (attrs & (1 << a)))    mesh_attr(m, a)->nr = nr;

    mesh_aabb_calc(m);
    *neighbors = NOCU(nb_buf);

    return cresp_val(mesh, NOCU(m));
}

static cerr fracture_layout(struct mesh *src, unsigned int *attrs)
{
    if (!mesh_nr_vx(src) || !mesh_nr_idx(src) || mesh_nr_idx(src) % 3 ||
        mesh_vx_stride(src) != sizeof(float) * 3 ||
        mesh_idx_stride(src) != sizeof(unsigned short))
        return CERR_INVALID_ARGUMENTS_REASON(
            .fmt    = "mesh '%s': unexpected layout",
            .arg0   = (void *)src->name
        );

    if (mesh_nr_weights(src) || mesh_nr_joints(src))
        return CERR_INVALID_ARGUMENTS_REASON(
            .fmt    = "mesh '%s': can't fracture a skinned mesh",
            .arg0   = (void *)src->name
        );

    *attrs = 0;
    if (mesh_nr_tx(src)) {
        if (mesh_tx_stride(src) != sizeof(float) * 2)   return CERR_INVALID_ARGUMENTS;
        *attrs |= MESH_TX_BIT;
    }
    if (mesh_nr_norm(src)) {
        if (mesh_norm_stride(src) != sizeof(float) * 3) return CERR_INVALID_ARGUMENTS;
        *attrs |= MESH_NORM_BIT;
    }
    if (mesh_nr_tangent(src)) {
        if (mesh_tangent_stride(src) != sizeof(float) * 4)  return CERR_INVALID_ARGUMENTS;
        *attrs |= MESH_TANGENTS_BIT;
    }

    return CERR_OK;
}

void fracture_fragments_release(fracture_fragments *ff)
{
    if (!ff)    return;

    for (size_t i = 0; i < ff->nr; i++) {
        ref_put(ff->fragments[i].mesh);
        mem_free(ff->fragments[i].neighbors);
    }

    mem_free(ff->fragments);
    ff->fragments = nullptr;
    ff->nr = 0;
}

static void cell_clearout(struct fcell *c)
{
    darray_clearout(c->vxs);
    darray_clearout(c->polys);
    darray_clearout(c->nvxs);
    darray_clearout(c->npolys);
    darray_clearout(c->segs);
    darray_clearout(c->side);
    darray_clearout(c->clip);
}

cres(fracture_fragments) fracture_mesh(struct mesh *src, const fracture_params *params, prng *rng)
{
    if (!src || !params)    return cres_error(fracture_fragments, CERR_INVALID_ARGUMENTS);

    size_t nr_seeds = params->seeds ? params->nr_seeds : params->nr_pieces;

    if (!nr_seeds || (!params->seeds && !rng))  return cres_error(fracture_fragments, CERR_INVALID_ARGUMENTS);

    struct fstate st = { .uv_scale = params->cap_uv_scale ? params->cap_uv_scale : 1.0f };

    CERR_RET_SCALAR(fracture_layout(src, &st.attrs), fracture_fragments);

    vec3 aabb[2], ext;

    vertex_array_aabb_calc(aabb, mesh_vx(src), mesh_vx_sz(src), mesh_vx_stride(src));
    vec3_sub(ext, aabb[1], aabb[0]);
    st.diag = vec3_len(ext);
    if (st.diag == 0.0) return cres_error(fracture_fragments, CERR_INVALID_ARGUMENTS);
    /* welded loop endpoints are bit-identical; this only absorbs noise */
    st.eps = st.diag * 1e-9;

    LOCAL(void, seeds_buf);
    /* cap neighbors are seed indices until the fragments are known */
    LOCAL(void, seed_frag_buf);
    const vec3 *seeds = params->seeds;

    if (!seeds) {
        vec3 *s = seeds_buf = mem_alloc(sizeof(vec3), .nr = nr_seeds);

        if (!s)     return cres_error(fracture_fragments, CERR_NOMEM);

        for (size_t i = 0; i < nr_seeds; i++)
            for (int j = 0; j < 3; j++) s[i][j] = aabb[0][j] + ext[j] * prng_unit(rng);
        seeds = s;
    }

    /* the source's triangles as polygons, copied into each cell */
    size_t nr_src = mesh_nr_idx(src) / 3;
    struct fcell base, cell;
    fracture_fragments ff = {};
    cerr err = CERR_OK;

    cell_init(&base);
    cell_init(&cell);

    for (size_t i = 0; i < nr_src && !IS_CERR(err); i++) {
        struct fvx v[3];

        for (int k = 0; k < 3; k++) fvx_load(&v[k], src, mesh_idx(src)[i * 3 + k], st.attrs);
        err = poly_emit(&base.vxs.da, &base.polys.da, v, 3, -1);
    }
    if (IS_CERR(err))   goto out;

    if (!darray_count(base.polys)) {
        err = CERR_INVALID_ARGUMENTS;
        goto out;
    }

    /* a clockwise source has its normals pointing inward from where we stand */
    double signed_vol = 0.0;
    for (size_t i = 0; i < darray_count(base.polys); i++) {
        const struct fpoly *p = &base.polys.x[i];
        const struct fvx *v = &base.vxs.x[p->start];
        dvec3 cross;

        d3_cross(cross, v[1].p, v[2].p);
        signed_vol += d3_dot(v[0].p, cross);
    }
    st.clockwise = signed_vol < 0.0;

    int *seed_frag = seed_frag_buf = mem_alloc(sizeof(*seed_frag), .nr = nr_seeds);

    ff.fragments = mem_alloc(sizeof(*ff.fragments), .nr = nr_seeds, .zero = 1);
    if (!ff.fragments || !seed_frag) {
        err = CERR_NOMEM;
        goto out;
    }

    for (size_t i = 0; i < nr_seeds && !IS_CERR(err); i++) {
        err = da_copy(&cell.vxs.da, &base.vxs.da);
        if (!IS_CERR(err))  err = da_copy(&cell.polys.da, &base.polys.da);

        for (size_t j = 0; j < nr_seeds && !IS_CERR(err); j++) {
            if (j == i)     continue;
            err = cell_clip(&cell, &st, seeds[i], seeds[j], j);
        }
        if (IS_CERR(err))   break;

        seed_frag[i] = -1;
        /* the cell missed the mesh */
        if (!darray_count(cell.polys))  continue;

        fracture_fragment *f = &ff.fragments[ff.nr];
        dvec3 center;

        f->volume = cell_volume(&cell, st.diag, center);
        d3_to(f->center, center);
        for (size_t k = 0; k < darray_count(cell.vxs); k++) {
            struct fvx *v = &cell.vxs.x[k];

            d3_sub(v->p, v->p, center);
        }

        cresp(mesh) mres = cell_mesh(&cell, src->name, st.attrs, &f->neighbors);
        if (IS_CERR(mres)) {
            err = cerr_error_cres(mres);
            break;
        }

        f->mesh = mres.val;
        seed_frag[i] = ff.nr++;
    }

    for (size_t i = 0; i < ff.nr && !IS_CERR(err); i++) {
        fracture_fragment *f = &ff.fragments[i];

        for (size_t t = 0; t < mesh_nr_idx(f->mesh) / 3; t++)
            if (f->neighbors[t] >= 0)   f->neighbors[t] = seed_frag[f->neighbors[t]];
    }

out:
    cell_clearout(&base);
    cell_clearout(&cell);

    if (IS_CERR(err)) {
        fracture_fragments_release(&ff);
        return cres_error_cerr(fracture_fragments, err);
    }

    return cres_val(fracture_fragments, ff);
}

/* a triangle of a kept fragment facing another kept one is hidden inside */
static bool merge_tri_hidden(const fracture_fragment *f, size_t t, const bool *keep)
{
    int nb = f->neighbors[t];

    return nb >= 0 && (!keep || keep[nb]);
}

/* (x, vertex) pairs, so the comparator carries its own context */
struct weld_key {
    double          x;
    unsigned int    idx;
};

static int weld_key_cmp(const void *_a, const void *_b)
{
    const struct weld_key *a = _a, *b = _b;

    return a->x < b->x ? -1 : a->x > b->x;
}

/*
 * Neighbouring fragments compute the points they share along different
 * routes, and the vertices went through single precision on their way
 * into the meshes, so a shared point can differ in the last bits
 * between them: snap every vertex within @eps of an earlier one onto
 * it, so the merged soup shares its points exactly.
 */
static cerr merge_weld(struct fvx *vxs, size_t nr_vxs, double eps)
{
    LOCAL(void, order_buf);
    struct weld_key *order = order_buf = mem_alloc(sizeof(*order), .nr = nr_vxs);

    if (!order)     return CERR_NOMEM;

    for (size_t i = 0; i < nr_vxs; i++) order[i] = (struct weld_key){ .x = vxs[i].p[0], .idx = i };

    qsort(order, nr_vxs, sizeof(*order), weld_key_cmp);

    for (size_t i = 1; i < nr_vxs; i++) {
        double *p = vxs[order[i].idx].p;

        for (size_t j = i; j-- > 0;) {
            const double *q = vxs[order[j].idx].p;
            dvec3 d;

            if (q[0] < p[0] - eps)  break;

            d3_sub(d, p, q);
            if (d3_len(d) <= eps) {
                d3_dup(p, q);
                break;
            }
        }
    }

    return CERR_OK;
}

cresp(mesh) fracture_merge(const fracture_fragments *ff, const bool *keep, const char *name)
{
    if (!ff || !name)   return cresp_error(mesh, CERR_INVALID_ARGUMENTS);

    unsigned int attrs = 0;
    size_t nr = 0, nr_kept = 0;
    vec3 lo = {}, hi = {};

    for (size_t i = 0; i < ff->nr; i++) {
        const fracture_fragment *f = &ff->fragments[i];

        if (keep && !keep[i])   continue;

        if (!nr_kept) {
            if (mesh_nr_tx(f->mesh))        attrs |= MESH_TX_BIT;
            if (mesh_nr_norm(f->mesh))      attrs |= MESH_NORM_BIT;
            if (mesh_nr_tangent(f->mesh))   attrs |= MESH_TANGENTS_BIT;
        }
        for (size_t t = 0; t < mesh_nr_idx(f->mesh) / 3; t++)
            if (!merge_tri_hidden(f, t, keep))  nr += 3;
        for (int j = 0; j < 3; j++) {
            float flo = f->mesh->aabb[0][j] + f->center[j], fhi = f->mesh->aabb[1][j] + f->center[j];

            lo[j] = nr_kept ? fminf(lo[j], flo) : flo;
            hi[j] = nr_kept ? fmaxf(hi[j], fhi) : fhi;
        }
        nr_kept++;
    }

    if (!nr_kept || !nr)    return cresp_error(mesh, CERR_INVALID_ARGUMENTS);
    if (nr > USHRT_MAX)     return cresp_error(mesh, CERR_TOO_LARGE);

    darray(struct fvx, vxs);
    cerr err = CERR_OK;

    darray_init(vxs);

    for (size_t i = 0; i < ff->nr && !IS_CERR(err); i++) {
        const fracture_fragment *f = &ff->fragments[i];
        dvec3 center;

        if (keep && !keep[i])   continue;

        d3_from(center, f->center);

        for (size_t t = 0; t < mesh_nr_idx(f->mesh) / 3 && !IS_CERR(err); t++) {
            if (merge_tri_hidden(f, t, keep))   continue;

            for (int k = 0; k < 3 && !IS_CERR(err); k++) {
                struct fvx v;

                fvx_load(&v, f->mesh, mesh_idx(f->mesh)[t * 3 + k], attrs);
                d3_add(v.p, v.p, center);
                err = da_push(&vxs.da, &v);
            }
        }
    }

    vec3 ext;

    vec3_sub(ext, hi, lo);
    /* above single precision noise, below any feature worth keeping */
    if (!IS_CERR(err))  err = merge_weld(vxs.x, darray_count(vxs), vec3_len(ext) * 1e-6);
    if (IS_CERR(err)) {
        darray_clearout(vxs);
        return cresp_error_cerr(mesh, err);
    }

    cresp(mesh) res = ref_new_checked(mesh, .name = name);

    if (IS_CERR(res)) {
        darray_clearout(vxs);
        return res;
    }

    struct mesh *m = res.val;

    err = mesh_attr_alloc(m, MESH_VX, sizeof(float) * 3, nr);
    if (!IS_CERR(err))                                  err = mesh_attr_alloc(m, MESH_IDX, sizeof(unsigned short), nr);
    if (!IS_CERR(err) && (attrs & MESH_TX_BIT))         err = mesh_attr_alloc(m, MESH_TX, sizeof(float) * 2, nr);
    if (!IS_CERR(err) && (attrs & MESH_NORM_BIT))       err = mesh_attr_alloc(m, MESH_NORM, sizeof(float) * 3, nr);
    if (!IS_CERR(err) && (attrs & MESH_TANGENTS_BIT))   err = mesh_attr_alloc(m, MESH_TANGENTS, sizeof(float) * 4, nr);
    if (IS_CERR(err)) {
        darray_clearout(vxs);
        ref_put(m);
        return cresp_error_cerr(mesh, err);
    }

    for (size_t i = 0; i < nr; i++) {
        fvx_store(&vxs.x[i], m, i, attrs);
        mesh_idx(m)[i] = i;
    }

    for (enum mesh_attrs a = 0; a < MESH_MAX; a++)
        if (a == MESH_VX || a == MESH_IDX || (attrs & (1 << a)))    mesh_attr(m, a)->nr = nr;

    mesh_aabb_calc(m);
    darray_clearout(vxs);

    return cresp_val(mesh, m);
}
