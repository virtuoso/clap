// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <stdlib.h>
#include "poly2d.h"
#include "test.h"
#include "util.h"

/*
 * Reference implementations of what poly2d generalizes: regular N-gons
 * around the origin, the span of one along an axis or across a frame, and
 * the frame between two rings of them that a ring layout runs an edge in.
 */
typedef struct ref_frame {
    vec2    A, dir, in;
} ref_frame;

static void ref_ngon_vertex(int j, int n, float r, vec2 out)
{
    float th = -(float)M_PI * 0.5f - (float)M_PI / n + j * 2.0f * (float)M_PI / n;

    out[0] = r * cosf(th);
    out[1] = r * sinf(th);
}

static float ref_poly_span(int n, float r, int axis, float at, float *lo, float *hi)
{
    int o = axis ^ 1;
    float mn = 0.0f, mx = 0.0f;
    bool any = false;

    for (int j = 0; j < n; j++) {
        vec2 a, b;

        ref_ngon_vertex(j, n, r, a);
        ref_ngon_vertex(j + 1, n, r, b);
        if ((a[o] <= at) == (b[o] <= at))   continue;

        float x = a[axis] + (b[axis] - a[axis]) * (at - a[o]) / (b[o] - a[o]);

        if (!any) { mn = mx = x; any = true; }
        else if (x < mn)    mn = x;
        else if (x > mx)    mx = x;
    }

    if (lo) *lo = mn;
    if (hi) *hi = mx;

    return any ? mx - mn : 0.0f;
}

static void ref_poly_bbox(int n, float r, vec2 lo, vec2 hi)
{
    lo[0] = lo[1] = hi[0] = hi[1] = 0.0f;
    for (int j = 0; j < n; j++) {
        vec2 v;

        ref_ngon_vertex(j, n, r, v);
        lo[0] = fminf(lo[0], v[0]);
        lo[1] = fminf(lo[1], v[1]);
        hi[0] = fmaxf(hi[0], v[0]);
        hi[1] = fmaxf(hi[1], v[1]);
    }
}

static int ref_poly_clip(const vec2 *src, int nin, const vec2 nrm, float d, vec2 *out)
{
    int nout = 0;

    for (int j = 0; j < nin; j++) {
        const float *a = src[j], *b = src[(j + 1) % nin];
        float da = a[0] * nrm[0] + a[1] * nrm[1] - d;
        float db = b[0] * nrm[0] + b[1] * nrm[1] - d;

        if (da <= 0.0f) {
            out[nout][0] = a[0];
            out[nout][1] = a[1];
            nout++;
        }
        if ((da <= 0.0f) != (db <= 0.0f)) {
            float t = da / (da - db);

            out[nout][0] = a[0] + (b[0] - a[0]) * t;
            out[nout][1] = a[1] + (b[1] - a[1]) * t;
            nout++;
        }
    }

    return nout;
}

static float ref_poly_span_frame(int sides, float r, const ref_frame *g, float across,
                                    float *lo, float *hi)
{
    float mn = 0.0f, mx = 0.0f;
    bool any = false;

    for (int j = 0; j < sides; j++) {
        vec2 a, b;

        ref_ngon_vertex(j, sides, r, a);
        ref_ngon_vertex(j + 1, sides, r, b);

        float ca = (a[0] - g->A[0]) * g->in[0] + (a[1] - g->A[1]) * g->in[1];
        float cb = (b[0] - g->A[0]) * g->in[0] + (b[1] - g->A[1]) * g->in[1];

        if ((ca <= across) == (cb <= across))   continue;

        float t = (across - ca) / (cb - ca);
        float al = (a[0] - g->A[0]) * g->dir[0] + (a[1] - g->A[1]) * g->dir[1];
        float bl = (b[0] - g->A[0]) * g->dir[0] + (b[1] - g->A[1]) * g->dir[1];
        float x = al + (bl - al) * t;

        if (!any) { mn = mx = x; any = true; }
        else if (x < mn)    mn = x;
        else if (x > mx)    mx = x;
    }

    if (lo) *lo = mn;
    if (hi) *hi = mx;

    return any ? mx - mn : 0.0f;
}

static bool ref_edge_cut(const vec2 a, const vec2 b, const ref_frame *g,
                            float al0, float al1, float acmax, float *t0, float *t1)
{
    float dA = g->A[0] * g->dir[0] + g->A[1] * g->dir[1];
    float aA = g->A[0] * g->in[0] + g->A[1] * g->in[1];
    /* keep dot(p, n) <= d, rows of { nx, nz, d } */
    float con[3][3] = {
        { -g->dir[0], -g->dir[1], -(dA + al0) },
        {  g->dir[0],  g->dir[1],   dA + al1  },
        {  g->in[0],   g->in[1],    aA + acmax },
    };
    float lo = 0.0f, hi = 1.0f;

    for (int k = 0; k < 3; k++) {
        float va = a[0] * con[k][0] + a[1] * con[k][1] - con[k][2];
        float vb = b[0] * con[k][0] + b[1] * con[k][1] - con[k][2];

        if (fabsf(vb - va) < 1e-6f) {
            if (va > 0.0f)  return false;
            continue;
        }

        float tc = va / (va - vb);

        if (vb > va)    hi = fminf(hi, tc);
        else            lo = fmaxf(lo, tc);
    }

    *t0 = fmaxf(lo, 0.0f);
    *t1 = fminf(hi, 1.0f);

    return *t1 - *t0 > 1e-3f;
}

/* a frame between two rings: vertex 2f of one ring toward vertex 2f + 1 of the next */
static void ref_frame_calc(ref_frame *g, int n, float r0, float r1, int f)
{
    vec2 B;

    ref_ngon_vertex(2 * f, n, r0, g->A);
    ref_ngon_vertex(2 * f + 1, n, r1, B);

    g->dir[0] = B[0] - g->A[0];
    g->dir[1] = B[1] - g->A[1];

    float len = sqrtf(g->dir[0] * g->dir[0] + g->dir[1] * g->dir[1]);

    g->dir[0] /= len;
    g->dir[1] /= len;
    g->in[0] = -g->dir[1];
    g->in[1] = g->dir[0];
}

static bool poly_near(float a, float b, float eps)
{
    return fabsf(a - b) <= eps;
}

static bool poly_vec2_near(const vec2 a, const vec2 b, float eps)
{
    return poly_near(a[0], b[0], eps) && poly_near(a[1], b[1], eps);
}

/* shoelace: positive for counterclockwise */
static float poly_area(const vec2 *pts, size_t n)
{
    float area = 0.0f;

    for (size_t j = 0; j < n; j++) {
        const float *a = pts[j], *b = pts[(j + 1) % n];

        area += a[0] * b[1] - b[0] * a[1];
    }

    return area * 0.5f;
}

static int poly2d_regular_test(void)
{
    const float r = 2.5f;

    for (int n = 3; n <= 16; n++) {
        float phase = poly2d_phase_edge0_neg_z(n);
        vec2 pts[16];

        /* the reference's vertices, also past either end of the ring */
        for (int j = -n; j <= 2 * n; j++) {
            vec2 v, ref;

            poly2d_regular_vertex(v, j, n, r, phase);
            ref_ngon_vertex(j, n, r, ref);
            if (!poly_vec2_near(v, ref, 1e-6f))                         return EXIT_FAILURE;
            if (!poly_near(sqrtf(v[0] * v[0] + v[1] * v[1]), r, 1e-5f)) return EXIT_FAILURE;
        }

        poly2d_regular(pts, n, r, phase);
        for (int j = 0; j < n; j++) {
            vec2 v;

            poly2d_regular_vertex(v, j, n, r, phase);
            if (memcmp(v, pts[j], sizeof(v)))                           return EXIT_FAILURE;
        }

        /* edge 0 faces -z: its midpoint is on the negative z axis at the apothem */
        float mx = (pts[0][0] + pts[1][0]) * 0.5f, mz = (pts[0][1] + pts[1][1]) * 0.5f;
        if (!poly_near(mx, 0.0f, 1e-5f))                                return EXIT_FAILURE;
        if (!poly_near(mz, -r * cosf((float)M_PI / n), 1e-5f))          return EXIT_FAILURE;

        /* counterclockwise, with the regular polygon's area */
        float area = 0.5f * n * r * r * sinf(2.0f * (float)M_PI / n);
        if (!poly_near(poly_area(pts, n), area, 1e-4f))                 return EXIT_FAILURE;
    }

    /* a phase of 0 puts vertex 0 on the first axis */
    vec2 v;
    poly2d_regular_vertex(v, 0, 5, 3.0f, 0.0f);
    if (!poly_vec2_near(v, (vec2){ 3.0f, 0.0f }, 1e-6f))                return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int poly2d_bbox_test(void)
{
    /* off the origin: a box seeded from (0, 0) would reach back to it */
    const vec2 quad[] = { { 10.0f, 20.0f }, { 12.0f, 20.5f }, { 11.5f, 23.0f }, { 9.5f, 22.0f } };
    vec2 lo, hi;

    poly2d_bbox(quad, array_size(quad), lo, hi);
    if (!poly_vec2_near(lo, (vec2){ 9.5f, 20.0f }, 0.0f))               return EXIT_FAILURE;
    if (!poly_vec2_near(hi, (vec2){ 12.0f, 23.0f }, 0.0f))              return EXIT_FAILURE;

    const vec2 neg[] = { { -4.0f, -3.0f }, { -2.0f, -3.0f }, { -3.0f, -1.0f } };
    poly2d_bbox(neg, array_size(neg), lo, hi);
    if (!poly_vec2_near(lo, (vec2){ -4.0f, -3.0f }, 0.0f))              return EXIT_FAILURE;
    if (!poly_vec2_near(hi, (vec2){ -2.0f, -1.0f }, 0.0f))              return EXIT_FAILURE;

    /* around the origin it agrees with the reference's */
    for (int n = 3; n <= 16; n++) {
        vec2 pts[16], rlo, rhi;

        poly2d_regular(pts, n, 7.0f, poly2d_phase_edge0_neg_z(n));
        poly2d_bbox(pts, n, lo, hi);
        ref_poly_bbox(n, 7.0f, rlo, rhi);
        if (!poly_vec2_near(lo, rlo, 1e-6f) || !poly_vec2_near(hi, rhi, 1e-6f)) return EXIT_FAILURE;
    }

    poly2d_bbox(nullptr, 0, lo, hi);
    if (lo[0] || lo[1] || hi[0] || hi[1])                               return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int poly2d_clip_test(void)
{
    vec2 hex[6], out[8], ref[8];

    poly2d_regular(hex, 6, 1.0f, poly2d_phase_edge0_neg_z(6));

    float area = poly_area(hex, 6);

    /* any line through the center halves a centrally symmetric polygon; winding stays */
    for (int k = 0; k < 12; k++) {
        float th = k * 0.37f;
        poly2d_halfplane hp = { .n = { cosf(th), sinf(th) }, .d = 0.0f };

        size_t nr = CRES_RET(poly2d_clip(hex, 6, &hp, out, array_size(out)), return EXIT_FAILURE);
        if (nr < 4 || nr > 7)                                           return EXIT_FAILURE;
        if (!poly_near(poly_area(out, nr), area * 0.5f, 1e-5f))         return EXIT_FAILURE;

        /* and agrees with the reference's */
        int nref = ref_poly_clip(hex, 6, hp.n, hp.d, ref);
        if ((int)nr != nref)                                            return EXIT_FAILURE;
        for (size_t j = 0; j < nr; j++)
            if (!poly_vec2_near(out[j], ref[j], 1e-6f))                 return EXIT_FAILURE;

        /* exactly enough room fits, one point less doesn't */
        CRES_RET(poly2d_clip(hex, 6, &hp, out, nr), return EXIT_FAILURE);
        cres(size_t) res = poly2d_clip(hex, 6, &hp, out, nr - 1);
        if (!IS_CERR_CODE(res, CERR_TOO_LARGE))                         return EXIT_FAILURE;
    }

    /* a clockwise polygon stays clockwise */
    vec2 cw[6];
    for (int j = 0; j < 6; j++) memcpy(cw[j], hex[5 - j], sizeof(cw[j]));

    poly2d_halfplane right = { .n = { -1.0f, 0.0f }, .d = 0.0f };
    size_t nr = CRES_RET(poly2d_clip(cw, 6, &right, out, array_size(out)), return EXIT_FAILURE);
    if (!poly_near(poly_area(out, nr), -area * 0.5f, 1e-5f))            return EXIT_FAILURE;

    /* all of it, or none of it */
    poly2d_halfplane all = { .n = { 0.0f, 1.0f }, .d = 2.0f };
    nr = CRES_RET(poly2d_clip(hex, 6, &all, out, 6), return EXIT_FAILURE);
    if (nr != 6 || memcmp(out, hex, sizeof(hex)))                       return EXIT_FAILURE;

    poly2d_halfplane none = { .n = { 0.0f, 1.0f }, .d = -2.0f };
    nr = CRES_RET(poly2d_clip(hex, 6, &none, out, 0), return EXIT_FAILURE);
    if (nr)                                                             return EXIT_FAILURE;

    nr = CRES_RET(poly2d_clip(nullptr, 0, &all, nullptr, 0), return EXIT_FAILURE);
    if (nr)                                                             return EXIT_FAILURE;

    cres(size_t) res = poly2d_clip(hex, 6, nullptr, out, array_size(out));
    if (!IS_CERR_CODE(res, CERR_INVALID_ARGUMENTS))                     return EXIT_FAILURE;

    /* a landing apron: three planes in a ring frame, ping-ponged */
    const int n = 12;
    vec2 buf[2][20], rbuf[2][20];
    ref_frame g;

    ref_frame_calc(&g, n, 9.0f, 8.4f, 1);
    poly2d_regular(buf[0], n, 9.0f, poly2d_phase_edge0_neg_z(n));
    memcpy(rbuf[0], buf[0], sizeof(rbuf[0]));

    float dA = g.A[0] * g.dir[0] + g.A[1] * g.dir[1];
    float aA = g.A[0] * g.in[0] + g.A[1] * g.in[1];
    float al0 = -1.0f, al1 = 1.4f, acmax = 1.83f;
    const poly2d_halfplane apron[3] = {
        { .n = { -g.dir[0], -g.dir[1] }, .d = -(dA + al0) },
        { .n = { g.dir[0], g.dir[1] }, .d = dA + al1 },
        { .n = { g.in[0], g.in[1] }, .d = aA + acmax },
    };

    nr = n;
    for (int k = 0; k < 3; k++)
        nr = CRES_RET(poly2d_clip(buf[k & 1], nr, &apron[k], buf[~k & 1], array_size(buf[0])),
                      return EXIT_FAILURE);

    int nref = ref_poly_clip(rbuf[0], n, (vec2){ -g.dir[0], -g.dir[1] }, -(dA + al0), rbuf[1]);
    nref = ref_poly_clip(rbuf[1], nref, g.dir, dA + al1, rbuf[0]);
    nref = ref_poly_clip(rbuf[0], nref, g.in, aA + acmax, rbuf[1]);
    if (nr < 3 || (int)nr != nref)                                      return EXIT_FAILURE;
    for (size_t j = 0; j < nr; j++)
        if (!poly_vec2_near(buf[1][j], rbuf[1][j], 1e-6f))              return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int poly2d_chord_test(void)
{
    const int n = 12;
    const float r = 10.0f;
    vec2 pts[12];

    poly2d_regular(pts, n, r, poly2d_phase_edge0_neg_z(n));

    /* along an axis: in the axis-aligned frames */
    for (float at = -10.3f; at < 10.3f; at += 0.37f) {
        float lo, hi, rlo, rhi;

        /* along x at z = at */
        float len = poly2d_chord(pts, n, (vec2){ 0.0f, 0.0f }, (vec2){ 1.0f, 0.0f }, at, &lo, &hi);
        float rlen = ref_poly_span(n, r, 0, at, &rlo, &rhi);
        if (!poly_near(len, rlen, 1e-4f))                               return EXIT_FAILURE;
        if (!poly_near(lo, rlo, 1e-4f) || !poly_near(hi, rhi, 1e-4f))   return EXIT_FAILURE;

        /* along z at x = at: z turned left is -x */
        len = poly2d_chord(pts, n, (vec2){ 0.0f, 0.0f }, (vec2){ 0.0f, 1.0f }, -at, &lo, &hi);
        rlen = ref_poly_span(n, r, 1, at, &rlo, &rhi);
        if (!poly_near(len, rlen, 1e-4f))                               return EXIT_FAILURE;
        if (!poly_near(lo, rlo, 1e-4f) || !poly_near(hi, rhi, 1e-4f))   return EXIT_FAILURE;
    }

    /* across a ring frame */
    for (int f = 0; f < n / 2; f++) {
        ref_frame g;

        ref_frame_calc(&g, n, r, r - 0.6f, f);
        for (float across = -1.0f; across < 4.0f; across += 0.23f) {
            float lo, hi, rlo, rhi;
            float len = poly2d_chord(pts, n, g.A, g.dir, across, &lo, &hi);
            float rlen = ref_poly_span_frame(n, r, &g, across, &rlo, &rhi);

            if (!poly_near(len, rlen, 1e-4f))                               return EXIT_FAILURE;
            if (!poly_near(lo, rlo, 1e-4f) || !poly_near(hi, rhi, 1e-4f))   return EXIT_FAILURE;
        }
    }

    /* turning the polygon and the frame together changes nothing */
    vec2 org = { 3.0f, -2.0f }, dir = { 0.6f, 0.8f }, turned[12], torg, tdir;
    float th = 0.7f, c = cosf(th), s = sinf(th);

    for (int j = 0; j < n; j++) {
        turned[j][0] = c * pts[j][0] - s * pts[j][1] + 5.0f;
        turned[j][1] = s * pts[j][0] + c * pts[j][1] - 1.0f;
    }
    torg[0] = c * org[0] - s * org[1] + 5.0f;
    torg[1] = s * org[0] + c * org[1] - 1.0f;
    tdir[0] = c * dir[0] - s * dir[1];
    tdir[1] = s * dir[0] + c * dir[1];

    for (float off = -8.0f; off < 8.0f; off += 0.51f) {
        float lo, hi, tlo, thi;
        float len = poly2d_chord(pts, n, org, dir, off, &lo, &hi);
        float tlen = poly2d_chord(turned, n, torg, tdir, off, &tlo, &thi);

        if (!poly_near(len, tlen, 1e-4f))                               return EXIT_FAILURE;
        if (!poly_near(lo, tlo, 1e-4f) || !poly_near(hi, thi, 1e-4f))   return EXIT_FAILURE;
    }

    /* a line that misses has no chord */
    float lo = 1.0f, hi = 1.0f;
    if (poly2d_chord(pts, n, (vec2){ 0.0f, 0.0f }, (vec2){ 1.0f, 0.0f }, 11.0f, &lo, &hi))  return EXIT_FAILURE;
    if (lo || hi)                                                                           return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* clip (@ax, @az)-(@bx, @bz) to @hp: kept if @hit, then over [@t0, @t1] */
static bool seg_clips(const poly2d_halfplane *hp, size_t nr, float ax, float az, float bx, float bz,
                      bool hit, float t0, float t1)
{
    float c0, c1;

    if (poly2d_segment_clip((vec2){ ax, az }, (vec2){ bx, bz }, hp, nr, &c0, &c1) != hit)   return false;

    return !hit || (poly_near(c0, t0, 1e-6f) && poly_near(c1, t1, 1e-6f));
}

static int poly2d_segment_clip_test(void)
{
    /* the window 0 <= x <= 2, z <= 1 */
    const poly2d_halfplane win[3] = {
        { .n = { -1.0f, 0.0f }, .d = 0.0f },
        { .n = { 1.0f, 0.0f }, .d = 2.0f },
        { .n = { 0.0f, 1.0f }, .d = 1.0f },
    };

    /* across it, both ways */
    if (!seg_clips(win, 3, -1.0f, 0.0f, 3.0f, 0.0f, true, 0.25f, 0.75f))    return EXIT_FAILURE;
    if (!seg_clips(win, 3, 3.0f, 0.0f, -1.0f, 0.0f, true, 0.25f, 0.75f))    return EXIT_FAILURE;

    /* into it diagonally: z <= 1 cuts first */
    if (!seg_clips(win, 3, 0.5f, -1.0f, 1.5f, 3.0f, true, 0.0f, 0.5f))  return EXIT_FAILURE;

    /* parallel to z = 1: inside, on the line, outside */
    if (!seg_clips(win, 3, -1.0f, 0.5f, 3.0f, 0.5f, true, 0.25f, 0.75f))    return EXIT_FAILURE;
    if (!seg_clips(win, 3, -1.0f, 1.0f, 3.0f, 1.0f, true, 0.25f, 0.75f))    return EXIT_FAILURE;
    if (!seg_clips(win, 3, -1.0f, 1.5f, 3.0f, 1.5f, false, 0.0f, 0.0f))     return EXIT_FAILURE;

    /* parallel to x = 0 and x = 2 at once, between them */
    if (!seg_clips(win, 3, 1.0f, -2.0f, 1.0f, 3.0f, true, 0.0f, 0.6f))  return EXIT_FAILURE;

    /* beside it, ending on its edge, and short of it */
    if (!seg_clips(win, 3, 3.0f, 0.0f, 5.0f, 0.0f, false, 0.0f, 0.0f))      return EXIT_FAILURE;
    if (!seg_clips(win, 3, 2.0f, 0.0f, 4.0f, 0.0f, false, 0.0f, 0.0f))      return EXIT_FAILURE;
    if (!seg_clips(win, 3, -3.0f, 0.0f, -1.0f, 0.0f, false, 0.0f, 0.0f))    return EXIT_FAILURE;

    /* nothing to clip to */
    if (!seg_clips(win, 0, 3.0f, 0.0f, 5.0f, 0.0f, true, 0.0f, 1.0f))   return EXIT_FAILURE;

    /* a ring's edges against windows in a ring frame */
    const int n = 12;
    const float r = 9.0f;

    for (int f = 0; f < n / 2; f++) {
        ref_frame g;

        ref_frame_calc(&g, n, r, r - 0.6f, f);

        float dA = g.A[0] * g.dir[0] + g.A[1] * g.dir[1];
        float aA = g.A[0] * g.in[0] + g.A[1] * g.in[1];
        float al0 = -0.4f, al1 = 1.1f, acmax = 1.83f;
        const poly2d_halfplane board[3] = {
            { .n = { -g.dir[0], -g.dir[1] }, .d = -(dA + al0) },
            { .n = { g.dir[0], g.dir[1] }, .d = dA + al1 },
            { .n = { g.in[0], g.in[1] }, .d = aA + acmax },
        };
        int cuts = 0;

        for (int i = 0; i < n; i++) {
            vec2 va, vb;
            float t0, t1, rt0, rt1;

            poly2d_regular_vertex(va, i, n, r, poly2d_phase_edge0_neg_z(n));
            poly2d_regular_vertex(vb, i + 1, n, r, poly2d_phase_edge0_neg_z(n));

            bool cut = poly2d_segment_clip(va, vb, board, 3, &t0, &t1);
            bool rcut = ref_edge_cut(va, vb, &g, al0, al1, acmax, &rt0, &rt1);

            /* a layout may drop cuts of 1e-3 or less; these windows have none */
            if (cut != rcut)                            return EXIT_FAILURE;
            if (cut && (t0 != rt0 || t1 != rt1))        return EXIT_FAILURE;
            cuts += cut;
        }
        if (!cuts)  return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int poly2d_edge_frame_test(void)
{
    vec2 dir, left;
    float len = poly2d_edge_frame((vec2){ 1, 2 }, (vec2){ 4, 6 }, dir, left);

    if (!poly_near(len, 5.0f, 1e-6f))                                   return EXIT_FAILURE;
    if (!poly_vec2_near(dir, (vec2){ 0.6f, 0.8f }, 1e-6f))              return EXIT_FAILURE;
    if (!poly_vec2_near(left, (vec2){ -0.8f, 0.6f }, 1e-6f))            return EXIT_FAILURE;

    /* the left of a counterclockwise polygon's edge is its inside */
    vec2 pts[7];
    poly2d_regular(pts, 7, 2.0f, 0.3f);
    for (int j = 0; j < 7; j++) {
        const float *a = pts[j], *b = pts[(j + 1) % 7];

        poly2d_edge_frame(a, b, dir, left);
        if (left[0] * -a[0] + left[1] * -a[1] <= 0.0f)                  return EXIT_FAILURE;
    }

    /* an inline edge frame */
    ref_frame g;
    ref_frame_calc(&g, 12, 9.0f, 8.4f, 2);

    vec2 B;
    ref_ngon_vertex(5, 12, 8.4f, B);
    poly2d_edge_frame(g.A, B, dir, left);
    if (!poly_vec2_near(dir, g.dir, 0.0f) || !poly_vec2_near(left, g.in, 0.0f)) return EXIT_FAILURE;

    len = poly2d_edge_frame((vec2){ 1, 2 }, (vec2){ 1, 2 }, dir, left);
    if (len || dir[0] || dir[1] || left[0] || left[1])                  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "poly2d regular", .test = poly2d_regular_test },
    { .name = "poly2d bbox", .test = poly2d_bbox_test },
    { .name = "poly2d clip", .test = poly2d_clip_test },
    { .name = "poly2d chord", .test = poly2d_chord_test },
    { .name = "poly2d segment clip", .test = poly2d_segment_clip_test },
    { .name = "poly2d edge frame", .test = poly2d_edge_frame_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
