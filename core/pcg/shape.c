// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include "memory.h"
#include "primitives.h"
#include "util.h"
#include "pcg/internal.h"
#include "pcg/shape.h"

/****************************************************************************
 * Plate
 ****************************************************************************/

/* a fan from the ring's first point, wound counterclockwise like poly_triangulate()'s */
static cres(poly_tris) plate_fan(const vec2 *pts, const poly_ring *ring)
{
    size_t nr_tris = ring->nr - 2;
    unsigned int *idx = mem_alloc(sizeof(*idx), .nr = nr_tris * 3);
    if (!idx)   return cres_error(poly_tris, CERR_NOMEM);

    bool ccw = poly_ring_area(pts, ring) >= 0.0f;
    for (size_t k = 1; k + 1 < ring->nr; k++) {
        unsigned int *t = &idx[(k - 1) * 3];

        t[0] = ring->start;
        t[1] = ring->start + (ccw ? k : k + 1);
        t[2] = ring->start + (ccw ? k + 1 : k);
    }

    poly_tris tris = { .idx = idx, .nr_tris = nr_tris };

    return cres_val(poly_tris, tris);
}

/*
 * One triangle of the top or the bottom: counterclockwise in (a, c), so
 * facing down as it is and up reversed; UVs are the sink (x, z), or the
 * local (a, c) through @chart
 */
static void plate_tri(const pcg_frame *f, const vec2 *pts, const unsigned int *ti, float y, bool top,
                      const pcg_chart *chart, bool swap, const prim_emit_opts *opts)
{
    prim_emit_opts po = *opts;
    vec3 v[3];
    vec2 uv[3];

    for (int k = 0; k < 3; k++) {
        const float *p = pts[ti[k]];
        float u, w;

        pcg_frame_point(f, (vec3){ p[0], y, p[1] }, v[k]);
        u = chart ? (p[0] - chart->org[0]) * chart->scale[0] : v[k][0];
        w = chart ? (p[1] - chart->org[1]) * chart->scale[1] : v[k][2];
        uv[k][0] = swap ? w : u;
        uv[k][1] = swap ? u : w;
    }

    if (top) {
        vec2 ruv[3] = { { uv[0][0], uv[0][1] }, { uv[2][0], uv[2][1] }, { uv[1][0], uv[1][1] } };

        po.uv = ruv;
        _prim_emit_triangle3(v[0], v[2], v[1], &po);
    } else {
        po.uv = uv;
        _prim_emit_triangle3(v[0], v[1], v[2], &po);
    }
}

/* the top or the bottom: one reservation for all of its triangles */
static cerr plate_cap(const pcg_ctx *ctx, const pcg_plate_desc *d, const poly_tris *tris, bool top)
{
    const pcg_surface *surf = top ? &d->top : &d->bottom;

    if (surf->skip || !tris->nr_tris)   return CERR_OK;

    pcg_target t = CRES_RET_CERR(pcg_sink_reserve(ctx, surf, PCG_UV_METERS, tris->nr_tris * 3));
    prim_emit_opts opts = { .mesh = t.mesh, .clockwise = t.clockwise };

    for (size_t i = 0; i < tris->nr_tris; i++)
        plate_tri(&ctx->frame, d->pts, &tris->idx[i * 3], top ? d->y1 : d->y0, top, d->chart, t.uv_swap, &opts);

    return CERR_OK;
}

/* the side faces of every ring, walked so that they face away from the body */
static cerr plate_edges(const pcg_ctx *ctx, const pcg_plate_desc *d, const poly_ring *rings, size_t nr_rings)
{
    if (d->edge.skip || d->y1 == d->y0)     return CERR_OK;

    LOCAL_SET(void, hole_buf) = mem_alloc(sizeof(bool), .nr = nr_rings);
    if (!hole_buf)  return CERR_NOMEM;

    bool *hole = hole_buf;
    CERR_RET_CERR(poly_rings_classify(d->pts, rings, nr_rings, hole));

    size_t nr_vx = 0;
    for (size_t r = 0; r < nr_rings; r++)   nr_vx += rings[r].nr * 6;

    pcg_target t = CRES_RET_CERR(pcg_sink_reserve(ctx, &d->edge, PCG_UV_METERS, nr_vx));
    prim_emit_opts opts = { .mesh = t.mesh, .clockwise = t.clockwise };

    for (size_t r = 0; r < nr_rings; r++) {
        const poly_ring *ring = &rings[r];
        /* a fill ring faces out counterclockwise, a hole clockwise */
        bool forward = (poly_ring_area(d->pts, ring) >= 0.0f) != hole[r];

        for (size_t k = 0; k < ring->nr; k++) {
            size_t k1 = (k + 1) % ring->nr;
            const float *a = d->pts[ring->start + (forward ? k : k1)];
            const float *b = d->pts[ring->start + (forward ? k1 : k)];
            float len = sqrtf((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]));

            pcg_frame_face(&ctx->frame, (vec3){ a[0], d->y0, a[1] }, (vec3){ b[0], d->y0, b[1] },
                           (vec3){ b[0], d->y1, b[1] }, (vec3){ a[0], d->y1, a[1] },
                           (vec4){ 0.0f, d->y0, len, d->y1 }, t.uv_swap, &opts);
        }
    }

    return CERR_OK;
}

cerr _pcg_plate(const pcg_ctx *ctx, const pcg_plate_desc *d)
{
    if (!ctx || !d || !d->pts || d->nr_pts < 3)
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "plate of fewer than 3 points");
    if (!pcg_ordered(d->y0, d->y1)) return CERR_INVALID_ARGUMENTS_REASON(.fmt = "plate top below its bottom");

    poly_ring whole = { .start = 0, .nr = d->nr_pts };
    const poly_ring *rings = d->rings ? d->rings : &whole;
    size_t nr_rings = d->rings ? d->nr_rings : 1;

    if (!nr_rings)  return CERR_INVALID_ARGUMENTS_REASON(.fmt = "plate without rings");
    for (size_t r = 0; r < nr_rings; r++)
        if (rings[r].nr < 3 || rings[r].start > d->nr_pts || rings[r].nr > d->nr_pts - rings[r].start)
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "plate ring %zu has fewer than 3 points or reaches past them",
                .arg0   = (void *)(uintptr_t)r
            );
    if (d->fan && nr_rings != 1)    return CERR_INVALID_ARGUMENTS_REASON(.fmt = "fan-triangulated plate with holes");

    bool caps = !d->top.skip || !d->bottom.skip;
    poly_tris tris = {};

    if (caps && d->fan)     tris = CRES_RET_CERR(plate_fan(d->pts, &rings[0]));
    else if (caps)          tris = CRES_RET_CERR(poly_triangulate(d->pts, rings, nr_rings));

    LOCAL_SET(void, tris_idx) = tris.idx;

    CERR_RET_CERR(plate_cap(ctx, d, &tris, false));
    CERR_RET_CERR(plate_cap(ctx, d, &tris, true));

    return plate_edges(ctx, d, rings, nr_rings);
}

/****************************************************************************
 * Band
 ****************************************************************************/

static void shape_swapf(float *a, float *b)
{
    float t = *a;

    *a = *b;
    *b = t;
}

cerr _pcg_band(const pcg_ctx *ctx, const pcg_band_desc *d)
{
    if (!ctx || !d)     return CERR_INVALID_ARGUMENTS;

    float a0 = d->lo[0], y0 = d->lo[1], c0 = d->lo[2];
    float a1 = d->hi[0], y1 = d->hi[1], c1 = d->hi[2];
    if (a0 > a1)    shape_swapf(&a0, &a1);
    if (y0 > y1)    shape_swapf(&y0, &y1);
    if (c0 > c1)    shape_swapf(&c0, &c1);

    if (!(a0 < a1) || !(y0 < y1) || !(c0 < c1)) return CERR_INVALID_ARGUMENTS_REASON(.fmt = "band with an empty range");

    unsigned int skip = d->skip_mask;
    size_t nr_vx = 0;
    for (int face = 0; face < 6; face++)
        if (!(skip & (1u << face)))     nr_vx += 6;

    if (d->surf.skip || !nr_vx)     return CERR_OK;

    pcg_target t = CRES_RET_CERR(pcg_sink_reserve(ctx, &d->surf, PCG_UV_METERS, nr_vx));
    prim_emit_opts opts = { .mesh = t.mesh, .clockwise = t.clockwise };
    const pcg_frame *f = &ctx->frame;
    float w = c1 - c0;

    /* the +c and -c faces */
    if (!(skip & PRIM_BOX_POS_C))
        pcg_frame_face(f, (vec3){ a1, y0, c1 }, (vec3){ a0, y0, c1 }, (vec3){ a0, y1, c1 }, (vec3){ a1, y1, c1 },
                       (vec4){ a0, y0, a1, y1 }, t.uv_swap, &opts);
    if (!(skip & PRIM_BOX_NEG_C))
        pcg_frame_face(f, (vec3){ a0, y0, c0 }, (vec3){ a1, y0, c0 }, (vec3){ a1, y1, c0 }, (vec3){ a0, y1, c0 },
                       (vec4){ a0, y0, a1, y1 }, t.uv_swap, &opts);
    /* bottom and top */
    if (!(skip & PRIM_BOX_NEG_Y))
        pcg_frame_face(f, (vec3){ a0, y0, c0 }, (vec3){ a0, y0, c1 }, (vec3){ a1, y0, c1 }, (vec3){ a1, y0, c0 },
                       (vec4){ a0, 0.0f, a1, w }, t.uv_swap, &opts);
    if (!(skip & PRIM_BOX_POS_Y))
        pcg_frame_face(f, (vec3){ a0, y1, c0 }, (vec3){ a1, y1, c0 }, (vec3){ a1, y1, c1 }, (vec3){ a0, y1, c1 },
                       (vec4){ a0, 0.0f, a1, w }, t.uv_swap, &opts);
    /* end caps */
    if (!(skip & PRIM_BOX_NEG_A))
        pcg_frame_face(f, (vec3){ a0, y0, c0 }, (vec3){ a0, y1, c0 }, (vec3){ a0, y1, c1 }, (vec3){ a0, y0, c1 },
                       (vec4){ 0.0f, y0, w, y1 }, t.uv_swap, &opts);
    if (!(skip & PRIM_BOX_POS_A))
        pcg_frame_face(f, (vec3){ a1, y0, c1 }, (vec3){ a1, y1, c1 }, (vec3){ a1, y1, c0 }, (vec3){ a1, y0, c0 },
                       (vec4){ 0.0f, y0, w, y1 }, t.uv_swap, &opts);

    return CERR_OK;
}

/****************************************************************************
 * Box
 ****************************************************************************/

/* the @faces (&enum prim_box_face bits) of the box [@lo, @hi], charted as prim_emit_box() charts them */
static void box_faces(const pcg_frame *f, const vec3 lo, const vec3 hi, unsigned int faces, bool swap,
                      const prim_emit_opts *opts)
{
    float a0 = lo[0], y0 = lo[1], c0 = lo[2];
    float a1 = hi[0], y1 = hi[1], c1 = hi[2];

    if (faces & PRIM_BOX_POS_A)
        pcg_frame_face(f, (vec3){ a1, y0, c0 }, (vec3){ a1, y0, c1 }, (vec3){ a1, y1, c1 }, (vec3){ a1, y1, c0 },
                       (vec4){ c0, y0, c1, y1 }, swap, opts);
    if (faces & PRIM_BOX_NEG_A)
        pcg_frame_face(f, (vec3){ a0, y0, c1 }, (vec3){ a0, y0, c0 }, (vec3){ a0, y1, c0 }, (vec3){ a0, y1, c1 },
                       (vec4){ -c1, y0, -c0, y1 }, swap, opts);
    if (faces & PRIM_BOX_POS_Y)
        pcg_frame_face(f, (vec3){ a0, y1, c0 }, (vec3){ a1, y1, c0 }, (vec3){ a1, y1, c1 }, (vec3){ a0, y1, c1 },
                       (vec4){ a0, c0, a1, c1 }, swap, opts);
    if (faces & PRIM_BOX_NEG_Y)
        pcg_frame_face(f, (vec3){ a0, y0, c0 }, (vec3){ a0, y0, c1 }, (vec3){ a1, y0, c1 }, (vec3){ a1, y0, c0 },
                       (vec4){ c0, a0, c1, a1 }, swap, opts);
    if (faces & PRIM_BOX_POS_C)
        pcg_frame_face(f, (vec3){ a1, y0, c1 }, (vec3){ a0, y0, c1 }, (vec3){ a0, y1, c1 }, (vec3){ a1, y1, c1 },
                       (vec4){ -a1, y0, -a0, y1 }, swap, opts);
    if (faces & PRIM_BOX_NEG_C)
        pcg_frame_face(f, (vec3){ a0, y0, c0 }, (vec3){ a1, y0, c0 }, (vec3){ a1, y1, c0 }, (vec3){ a0, y1, c0 },
                       (vec4){ a0, y0, a1, y1 }, swap, opts);
}

cerr _pcg_box(const pcg_ctx *ctx, const pcg_box_desc *d)
{
    if (!ctx || !d)     return CERR_INVALID_ARGUMENTS;

    vec3 lo, hi;
    for (int k = 0; k < 3; k++) {
        lo[k] = d->lo[k];
        hi[k] = d->hi[k];
        if (lo[k] > hi[k])      shape_swapf(&lo[k], &hi[k]);
        if (!(lo[k] < hi[k]))   return CERR_INVALID_ARGUMENTS_REASON(.fmt = "box with an empty range");
    }

    /* one reservation per surface, for all of its faces */
    unsigned int done = 0;
    for (int k = 0; k < 6; k++) {
        const pcg_surface *surf = d->face[k];

        if ((done & (1u << k)) || !surf || surf->skip)  continue;

        unsigned int group = 0;
        size_t nr_vx = 0;
        for (int j = k; j < 6; j++)
            if (d->face[j] == surf) {
                group |= 1u << j;
                nr_vx += 6;
            }
        done |= group;

        pcg_target t = CRES_RET_CERR(pcg_sink_reserve(ctx, surf, PCG_UV_METERS, nr_vx));
        box_faces(&ctx->frame, lo, hi, group, t.uv_swap, &(prim_emit_opts){ .mesh = t.mesh, .clockwise = t.clockwise });
    }

    return CERR_OK;
}

/****************************************************************************
 * Pane
 ****************************************************************************/

/* the most pieces a pane of @nr openings splits into: runs between them and transoms over them */
static size_t pane_max_pieces(size_t nr)
{
    return 2 * nr + 1;
}

/* a piece @w wide is emitted: it has a width, and at least @min_seg of it */
static bool pane_keeps(float w, float min_seg)
{
    return w > 0.0f && w >= min_seg;
}

cerr _pcg_pane(const pcg_ctx *ctx, const pcg_pane_desc *d)
{
    if (!ctx || !d)     return CERR_INVALID_ARGUMENTS;
    if (!pcg_positive(d->len) || !(d->y0 < d->y1) || !pcg_nonnegative(d->min_seg))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "pane without length or height, or a negative piece");
    CERR_RET_CERR(pcg_openings_check(d->open, d->nr_open, d->len));

    if (d->surf.skip)   return CERR_OK;

    LOCAL_SET(void, buf) = mem_alloc(sizeof(vec3), .nr = pane_max_pieces(d->nr_open));
    if (!buf)   return CERR_NOMEM;

    /* pieces as { a0, a1, bottom }: the runs between the openings, then the transoms */
    vec3 *piece = buf;
    size_t nr = 0;
    float at = 0.0f;

    for (size_t i = 0; i <= d->nr_open; i++) {
        pcg_span s = i < d->nr_open ? pcg_opening_span(&d->open[i]) : (pcg_span){ .lo = d->len };

        if (pane_keeps(s.lo - at, d->min_seg)) {
            piece[nr][0] = at;
            piece[nr][1] = s.lo;
            piece[nr++][2] = d->y0;
        }
        at = s.hi;
    }
    for (size_t i = 0; i < d->nr_open; i++) {
        pcg_span s = pcg_opening_span(&d->open[i]);
        float h = d->open[i].h;

        if (!(h > d->y0 && h < d->y1) || !pane_keeps(s.hi - s.lo, d->min_seg))  continue;
        piece[nr][0] = s.lo;
        piece[nr][1] = s.hi;
        piece[nr++][2] = h;
    }

    if (!nr)    return CERR_OK;

    pcg_target t = CRES_RET_CERR(pcg_sink_reserve(ctx, &d->surf, PCG_UV_METERS, nr * 12));
    prim_emit_opts opts = { .mesh = t.mesh, .clockwise = t.clockwise };
    const pcg_frame *f = &ctx->frame;
    float y1 = d->y1;

    for (size_t i = 0; i < nr; i++) {
        float a0 = piece[i][0], a1 = piece[i][1], ylo = piece[i][2];

        pcg_frame_face(f, (vec3){ a0, ylo, 0.0f }, (vec3){ a1, ylo, 0.0f }, (vec3){ a1, y1, 0.0f },
                       (vec3){ a0, y1, 0.0f }, (vec4){ a0, ylo, a1, y1 }, t.uv_swap, &opts);
        pcg_frame_face(f, (vec3){ a1, ylo, 0.0f }, (vec3){ a0, ylo, 0.0f }, (vec3){ a0, y1, 0.0f },
                       (vec3){ a1, y1, 0.0f }, (vec4){ -a1, ylo, -a0, y1 }, t.uv_swap, &opts);
    }

    return CERR_OK;
}

/****************************************************************************
 * Rod
 ****************************************************************************/

/* the point at angle @ang on the ring of radius @r around @c in the (@e0, @e1) plane */
static void rod_ring_pt(vec3 out, const vec3 c, const vec3 e0, const vec3 e1, float r, float ang)
{
    float ca = cosf(ang) * r, sa = sinf(ang) * r;

    out[0] = c[0] + e0[0] * ca + e1[0] * sa;
    out[1] = c[1] + e0[1] * ca + e1[1] * sa;
    out[2] = c[2] + e0[2] * ca + e1[2] * sa;
}

/* emit a triangle with its UVs, U and V swapped under @swap */
static void rod_tri(vec3 v0, vec3 v1, vec3 v2, const vec2 t0, const vec2 t1, const vec2 t2, bool swap,
                    const prim_emit_opts *opts)
{
    prim_emit_opts po = *opts;
    vec2 uv[3] = { { t0[0], t0[1] }, { t1[0], t1[1] }, { t2[0], t2[1] } };

    if (swap)
        for (int k = 0; k < 3; k++)     shape_swapf(&uv[k][0], &uv[k][1]);

    po.uv = uv;
    _prim_emit_triangle3(v0, v1, v2, &po);
}

cerr _pcg_rod(const pcg_ctx *ctx, const pcg_rod_desc *d)
{
    if (!ctx || !d)     return CERR_INVALID_ARGUMENTS;
    if (d->sides < 3 || !pcg_positive(d->ra) || !pcg_positive(d->rb))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "rod of fewer than 3 sides or without a radius");
    if (d->sides > PCG_ROD_SIDES_MAX)   return CERR_TOO_LARGE_REASON(.fmt = "rod of more sides than a mesh holds");

    vec3 a, b, axis, e0, e1, up = { 0.0f, 1.0f, 0.0f };

    pcg_frame_point(&ctx->frame, d->a, a);
    pcg_frame_point(&ctx->frame, d->b, b);
    vec3_sub(axis, b, a);

    float len = vec3_len(axis);
    if (!(len >= PCG_ROD_MIN_LEN))  return CERR_INVALID_ARGUMENTS_REASON(.fmt = "rod without length");
    vec3_scale(axis, axis, 1.0f / len);

    /*
     * the cross-section frame of _prim_emit_prism(), except that a steep
     * axis hangs it off the frame's run direction instead of world +x, so
     * the section turns with the frame; at the identity frame they agree
     */
    if (fabsf(vec3_mul_inner(axis, up)) > 0.9f) {
        up[0] = ctx->frame.dir[0]; up[1] = 0.0f; up[2] = ctx->frame.dir[1];
    }
    vec3_mul_cross(e0, up, axis);
    vec3_norm(e0, e0);
    vec3_mul_cross(e1, axis, e0);
    vec3_norm(e1, e1);

    if (d->surf.skip)   return CERR_OK;

    /* per side: 2 wall and 2 cap triangles */
    pcg_target t = CRES_RET_CERR(pcg_sink_reserve(ctx, &d->surf, PCG_UV_METERS, (size_t)d->sides * 12));
    prim_emit_opts opts = { .mesh = t.mesh };

    /* side widths at either end: V runs around the perimeter in meters */
    float wa = 2.0f * d->ra * sinf((float)M_PI / d->sides);
    float wb = 2.0f * d->rb * sinf((float)M_PI / d->sides);

    for (int s = 0; s < d->sides; s++) {
        float t0 = d->phase + (float)s / d->sides * 2.0f * (float)M_PI;
        float t1 = d->phase + (float)(s + 1) / d->sides * 2.0f * (float)M_PI;
        vec3 a0, a1, b0, b1;

        rod_ring_pt(a0, a, e0, e1, d->ra, t0);
        rod_ring_pt(a1, a, e0, e1, d->ra, t1);
        rod_ring_pt(b0, b, e0, e1, d->rb, t0);
        rod_ring_pt(b1, b, e0, e1, d->rb, t1);

        const vec2 ua0 = { 0.0f, s * wa }, ua1 = { 0.0f, (s + 1) * wa };
        const vec2 ub0 = { len, s * wb }, ub1 = { len, (s + 1) * wb };
        /* the caps' own planes, around the end centers */
        const vec2 ca0 = { cosf(t0) * d->ra, sinf(t0) * d->ra }, ca1 = { cosf(t1) * d->ra, sinf(t1) * d->ra };
        const vec2 cb0 = { cosf(t0) * d->rb, sinf(t0) * d->rb }, cb1 = { cosf(t1) * d->rb, sinf(t1) * d->rb };
        const vec2 center = { 0.0f, 0.0f };

        rod_tri(a0, a1, b1, ua0, ua1, ub1, t.uv_swap, &opts);     /* wall, wound outward */
        rod_tri(a0, b1, b0, ua0, ub1, ub0, t.uv_swap, &opts);
        rod_tri(a, a1, a0, center, ca1, ca0, t.uv_swap, &opts);   /* near cap */
        rod_tri(b, b0, b1, center, cb0, cb1, t.uv_swap, &opts);   /* far cap */
    }

    return CERR_OK;
}

/****************************************************************************
 * Cylinder
 ****************************************************************************/

cerr _pcg_cylinder(const pcg_ctx *ctx, const pcg_cylinder_desc *d)
{
    if (!ctx || !d)     return CERR_INVALID_ARGUMENTS;
    if (d->sides < 3 || !pcg_positive(d->r) || !pcg_positive(d->h))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "cylinder of fewer than 3 sides or without a radius or height");
    if (d->sides > PCG_CYLINDER_SIDES_MAX)
        return CERR_TOO_LARGE_REASON(.fmt = "cylinder of more sides than a mesh holds");

    if (d->surf.skip)   return CERR_OK;

    /* per side: 2 cap and 2 side triangles */
    pcg_target t = CRES_RET_CERR(pcg_sink_reserve(ctx, &d->surf, d->uv, (size_t)d->sides * 12));
    prim_emit_opts opts = { .mesh = t.mesh, .clockwise = t.clockwise };
    const pcg_frame *f = &ctx->frame;
    bool unit = d->uv == PCG_UV_UNIT;
    float a = d->org[0], c = d->org[2];
    float y0 = d->org[1], y1 = d->org[1] + d->h;
    /* side width: U runs around the perimeter in meters */
    float w = 2.0f * d->r * sinf((float)M_PI / d->sides);
    vec3 bot, top;

    pcg_frame_point(f, (vec3){ a, y0, c }, bot);
    pcg_frame_point(f, (vec3){ a, y1, c }, top);

    for (int s = 0; s < d->sides; s++) {
        /* the turn of _prim_emit_cylinder(), in double like there */
        float a1 = a + d->r * cos(M_PI * 2 * s / d->sides);
        float c1 = c + d->r * sin(M_PI * 2 * s / d->sides);
        float a2 = a + d->r * cos(M_PI * 2 * (s + 1) / d->sides);
        float c2 = c + d->r * sin(M_PI * 2 * (s + 1) / d->sides);
        vec3 b1, b2, t1, t2;

        pcg_frame_point(f, (vec3){ a1, y0, c1 }, b1);
        pcg_frame_point(f, (vec3){ a2, y0, c2 }, b2);
        pcg_frame_point(f, (vec3){ a1, y1, c1 }, t1);
        pcg_frame_point(f, (vec3){ a2, y1, c2 }, t2);

        /* the caps: the unit triangle, or the plane in meters from the axis */
        const vec2 axis = { 0.0f, 0.0f };
        const vec2 m1 = { a1 - a, c1 - c }, m2 = { a2 - a, c2 - c };
        const vec2 u01 = { 0.0f, 1.0f }, u10 = { 1.0f, 0.0f };
        /* the side quad: the unit square, or meters around and up */
        float u0 = unit ? 0.0f : s * w, u1 = unit ? 1.0f : (s + 1) * w;
        float v0 = unit ? 0.0f : y0, v1 = unit ? 1.0f : y1;
        const vec2 q0 = { u0, v0 }, q1 = { u1, v0 }, q2 = { u1, v1 }, q3 = { u0, v1 };

        rod_tri(bot, b1, b2, axis, unit ? u01 : m1, unit ? u10 : m2, t.uv_swap, &opts);
        rod_tri(b1, t1, b2, q0, q3, q1, t.uv_swap, &opts);
        rod_tri(t1, t2, b2, q3, q2, q1, t.uv_swap, &opts);
        rod_tri(top, t2, t1, axis, unit ? u01 : m2, unit ? u10 : m1, t.uv_swap, &opts);
    }

    return CERR_OK;
}
