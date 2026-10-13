// SPDX-License-Identifier: Apache-2.0
/*
 * Procedural content library tests, built as the standalone pcg_tests
 * binary: the pure pcg sources and the engine helpers they lean on, with
 * the shared test harness, no renderer.
 */
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mesh.h"
#include "poly2d.h"
#include "prng.h"
#include "primitives.h"
#include "test.h"
#include "util.h"
#include "pcg/atrium.h"
#include "pcg/balustrade.h"
#include "pcg/column.h"
#include "pcg/facade.h"
#include "pcg/fascia.h"
#include "pcg/fixture.h"
#include "pcg/flight.h"
#include "pcg/glazing.h"
#include "pcg/frame.h"
#include "pcg/furniture.h"
#include "pcg/gallery.h"
#include "pcg/material.h"
#include "pcg/office.h"
#include "pcg/room.h"
#include "pcg/room-row.h"
#include "pcg/shape.h"
#include "pcg/sign.h"
#include "pcg/sink.h"
#include "pcg/skylight.h"
#include "pcg/slab.h"
#include "pcg/span.h"
#include "pcg/stair.h"
#include "pcg/wall.h"
#include "pcg/window-wall.h"

static bool near(float a, float b, float eps)
{
    return fabsf(a - b) <= eps;
}

static bool failed(cerr err)
{
    return IS_CERR(err);
}

/* @err is the CERR_INVALID_ARGUMENTS a check turns bad input down with */
static bool failed_inval(cerr err)
{
    return IS_CERR_CODE(err, CERR_INVALID_ARGUMENTS);
}

static bool vec3_near(const vec3 a, const vec3 b, float eps)
{
    return near(a[0], b[0], eps) && near(a[1], b[1], eps) && near(a[2], b[2], eps);
}

/****************************************************************************
 * Frames
 ****************************************************************************/

/* yawed and mirrored test frames, unit directions */
static void test_frame(pcg_frame *f, float yaw, bool mirror, float x, float y, float z)
{
    *f = (pcg_frame){ .org = { x, y, z }, .dir = { cosf(yaw), sinf(yaw) }, .mirror = mirror };
}

/* frames pcg_frame_check() turns down: an origin, a direction that isn't finite, no direction */
static const pcg_frame bad_frames[] = {
    { .org = { NAN, 0.0f, 0.0f }, .dir = { 1.0f, 0.0f } },
    { .org = { 0.0f, -INFINITY, 0.0f }, .dir = { 1.0f, 0.0f } },
    { .dir = { 0.0f, INFINITY } },
    { .dir = { NAN, 0.0f } },
    { .org = { 1.0f, 2.0f, 3.0f } },
    { .dir = { 2.0f, 0.0f } },
};

static int frame_check_test(void)
{
    pcg_frame f;
    test_frame(&f, 2.5f, true, -4.0f, 3.0f, 9.0f);

    if (failed(pcg_frame_check(&f)) || failed(pcg_frame_check(&PCG_FRAME_IDENTITY)))    return EXIT_FAILURE;
    if (!failed_inval(pcg_frame_check(nullptr)))                                        return EXIT_FAILURE;
    for (size_t i = 0; i < array_size(bad_frames); i++)
        if (!failed_inval(pcg_frame_check(&bad_frames[i])))                             return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int frame_point_test(void)
{
    pcg_frame f;

    /* the run frame point formula of the box emitters, bit for bit */
    test_frame(&f, 0.7f, false, 3.0f, 0.0f, -2.0f);
    for (float a = -2.0f; a <= 2.0f; a += 0.5f)
        for (float c = -1.0f; c <= 1.0f; c += 0.25f) {
            vec3 p;
            float px = -f.dir[1], pz = f.dir[0];

            pcg_frame_point(&f, (vec3){ a, 1.5f, c }, p);
            if (p[0] != f.org[0] + a * f.dir[0] + c * px)   return EXIT_FAILURE;
            if (p[1] != 1.5f)                               return EXIT_FAILURE;
            if (p[2] != f.org[2] + a * f.dir[1] + c * pz)   return EXIT_FAILURE;
        }

    /* +c is the left of the run, the right under a mirror */
    vec2 across;
    f = PCG_FRAME_IDENTITY;
    pcg_frame_across(&f, across);
    if (across[0] != 0.0f || across[1] != 1.0f)     return EXIT_FAILURE;
    f.mirror = true;
    pcg_frame_across(&f, across);
    if (across[0] != 0.0f || across[1] != -1.0f)    return EXIT_FAILURE;

    /* points and directions survive the round trip, mirrored or not */
    for (int m = 0; m < 2; m++) {
        test_frame(&f, 2.3f, m, -4.0f, 6.0f, 1.0f);

        vec3 l = { 1.25f, -0.5f, 2.75f }, p, back;

        pcg_frame_point(&f, l, p);
        pcg_frame_point_to_local(&f, p, back);
        if (!vec3_near(back, l, 1e-5f))     return EXIT_FAILURE;

        pcg_frame_vector(&f, l, p);
        if (!near(vec3_len(p), vec3_len(l), 1e-5f))     return EXIT_FAILURE;
        pcg_frame_vector_to_local(&f, p, back);
        if (!vec3_near(back, l, 1e-5f))     return EXIT_FAILURE;

        /* in place */
        vec3_dup(p, l);
        pcg_frame_point(&f, p, p);
        pcg_frame_point_to_local(&f, p, p);
        if (!vec3_near(p, l, 1e-5f))        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int frame_compose_test(void)
{
    for (int pm = 0; pm < 2; pm++)
        for (int lm = 0; lm < 2; lm++) {
            pcg_frame parent, local, f;

            test_frame(&parent, -1.1f, pm, 10.0f, 3.0f, -5.0f);
            test_frame(&local, 0.4f, lm, 2.0f, 1.0f, -3.0f);
            pcg_frame_compose(&parent, &local, &f);

            if (f.mirror != (pm != lm))                         return EXIT_FAILURE;
            if (!near(vec2_len(f.dir), 1.0f, 1e-6f))            return EXIT_FAILURE;

            for (float a = -1.0f; a <= 1.0f; a += 1.0f)
                for (float c = -1.0f; c <= 1.0f; c += 0.5f) {
                    vec3 l = { a, 0.25f, c }, mid, two, one;

                    pcg_frame_point(&local, l, mid);
                    pcg_frame_point(&parent, mid, two);
                    pcg_frame_point(&f, l, one);
                    if (!vec3_near(one, two, 1e-5f))            return EXIT_FAILURE;
                }

            /* in place */
            pcg_frame_compose(&parent, &local, &local);
            if (!vec3_near(local.org, f.org, 0.0f) || local.mirror != f.mirror) return EXIT_FAILURE;
        }

    /* the identity changes nothing */
    pcg_frame f, g;
    test_frame(&f, 0.9f, true, 1.0f, 2.0f, 3.0f);
    pcg_frame_compose(&PCG_FRAME_IDENTITY, &f, &g);
    if (!vec3_near(g.org, f.org, 0.0f) || g.dir[0] != f.dir[0] || g.dir[1] != f.dir[1]) return EXIT_FAILURE;
    if (!g.mirror)                                                                      return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int frame_run_test(void)
{
    pcg_frame f = { .mirror = true };
    float len = CRES_RET(pcg_frame_run(&f, (vec2){ 1.0f, 2.0f }, (vec2){ 4.0f, 6.0f }, 7.0f),
                         return EXIT_FAILURE);

    if (!near(len, 5.0f, 1e-6f))                                        return EXIT_FAILURE;
    if (!vec3_near(f.org, (vec3){ 1.0f, 7.0f, 2.0f }, 0.0f))            return EXIT_FAILURE;
    if (!near(f.dir[0], 0.6f, 1e-6f) || !near(f.dir[1], 0.8f, 1e-6f))   return EXIT_FAILURE;
    if (f.mirror)                                                       return EXIT_FAILURE;

    /* the run's end lands at (len, 0, 0) */
    vec3 p;
    pcg_frame_point(&f, (vec3){ len, 0.0f, 0.0f }, p);
    if (!vec3_near(p, (vec3){ 4.0f, 7.0f, 6.0f }, 1e-5f))           return EXIT_FAILURE;

    pcg_frame g = f;
    cres(float) res = pcg_frame_run(&g, (vec2){ 1.0f, 2.0f }, (vec2){ 1.0f, 2.0f }, 0.0f);
    if (!IS_CERR_CODE(res, CERR_INVALID_ARGUMENTS))                 return EXIT_FAILURE;
    if (memcmp(&f, &g, sizeof(f)))                                  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Spans
 ****************************************************************************/

static bool span_eq(const pcg_span *s, float lo, float hi)
{
    return near(s->lo, lo, 1e-6f) && near(s->hi, hi, 1e-6f);
}

static int span_subtract_test(void)
{
    pcg_span run = { 0.0f, 10.0f }, out[8];

    /* no cuts: the whole run */
    size_t n = CRES_RET(pcg_span_subtract(&run, nullptr, 0, 0.05f, 0.15f, out, 8), return EXIT_FAILURE);
    if (n != 1 || !span_eq(&out[0], 0.0f, 10.0f))                   return EXIT_FAILURE;

    /* out of order and overlapping cuts merge; so do cuts closer than the merge distance */
    const pcg_span cuts[] = { { 6.0f, 7.0f }, { 2.0f, 3.0f }, { 2.5f, 4.0f }, { 7.04f, 8.0f } };
    n = CRES_RET(pcg_span_subtract(&run, cuts, array_size(cuts), 0.05f, 0.15f, out, 8), return EXIT_FAILURE);
    if (n != 3)                                                     return EXIT_FAILURE;
    if (!span_eq(&out[0], 0.0f, 2.0f) || !span_eq(&out[1], 4.0f, 6.0f) || !span_eq(&out[2], 8.0f, 10.0f))
        return EXIT_FAILURE;

    /* a gap just over the merge distance keeps its piece, unless the piece is too short */
    const pcg_span gap[] = { { 2.0f, 3.0f }, { 3.1f, 4.0f } };
    n = CRES_RET(pcg_span_subtract(&run, gap, 2, 0.05f, 0.0f, out, 8), return EXIT_FAILURE);
    if (n != 3 || !span_eq(&out[1], 3.0f, 3.1f))                    return EXIT_FAILURE;
    n = CRES_RET(pcg_span_subtract(&run, gap, 2, 0.05f, 0.15f, out, 8), return EXIT_FAILURE);
    if (n != 2 || !span_eq(&out[1], 4.0f, 10.0f))                   return EXIT_FAILURE;

    /* runs no longer than the minimum go, at either end */
    const pcg_span ends[] = { { 0.1f, 5.0f }, { 5.5f, 9.9f } };
    n = CRES_RET(pcg_span_subtract(&run, ends, 2, 0.05f, 0.15f, out, 8), return EXIT_FAILURE);
    if (n != 1 || !span_eq(&out[0], 5.0f, 5.5f))                    return EXIT_FAILURE;

    /* cuts past the run's ends are clipped to it */
    pcg_span off = { 2.0f, 6.0f };
    const pcg_span wide[] = { { -3.0f, 1.0f }, { 5.0f, 12.0f } };
    n = CRES_RET(pcg_span_subtract(&off, wide, 2, 0.0f, 0.0f, out, 8), return EXIT_FAILURE);
    if (n != 1 || !span_eq(&out[0], 2.0f, 5.0f))                    return EXIT_FAILURE;

    /* a cut over everything leaves nothing */
    const pcg_span all = { -1.0f, 11.0f };
    n = CRES_RET(pcg_span_subtract(&run, &all, 1, 0.0f, 0.0f, out, 8), return EXIT_FAILURE);
    if (n)                                                          return EXIT_FAILURE;

    /* errors */
    const pcg_span rev = { 3.0f, 2.0f };
    cres(size_t) res = pcg_span_subtract(&run, &rev, 1, 0.0f, 0.0f, out, 8);
    if (!IS_CERR_CODE(res, CERR_INVALID_ARGUMENTS))                 return EXIT_FAILURE;
    res = pcg_span_subtract(&(pcg_span){ 1.0f, 0.0f }, nullptr, 0, 0.0f, 0.0f, out, 8);
    if (!IS_CERR_CODE(res, CERR_INVALID_ARGUMENTS))                 return EXIT_FAILURE;
    res = pcg_span_subtract(&run, nullptr, 0, -0.1f, 0.0f, out, 8);
    if (!IS_CERR_CODE(res, CERR_INVALID_ARGUMENTS))                 return EXIT_FAILURE;
    res = pcg_span_subtract(&run, nullptr, 0, 0.0f, NAN, out, 8);
    if (!IS_CERR_CODE(res, CERR_INVALID_ARGUMENTS))                 return EXIT_FAILURE;
    res = pcg_span_subtract(&run, cuts, array_size(cuts), 0.05f, 0.15f, out, 2);
    if (!IS_CERR_CODE(res, CERR_TOO_LARGE))                         return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/*
 * Reference copies of the original layout's edge cut and rail run split,
 * with their unbounded window ends as +-1e9 sentinels: pcg_region_cut()
 * and pcg_segment_runs() must give the same numbers with open ranges.
 */
static void ref_ngon_vertex(int j, int n, float r, vec2 out)
{
    float th = -(float)M_PI * 0.5f - (float)M_PI / n + j * 2.0f * (float)M_PI / n;

    out[0] = r * cosf(th);
    out[1] = r * sinf(th);
}

typedef struct ref_window {
    vec2    A, dir, in;
    float   al0, al1, acmax;
} ref_window;

static bool ref_edge_cut(const vec2 a, const vec2 b, const ref_window *w, float *t0, float *t1)
{
    float dA = w->A[0] * w->dir[0] + w->A[1] * w->dir[1];
    float aA = w->A[0] * w->in[0] + w->A[1] * w->in[1];
    float con[3][3] = {
        { -w->dir[0], -w->dir[1], -(dA + w->al0) },
        {  w->dir[0],  w->dir[1],   dA + w->al1  },
        {  w->in[0],   w->in[1],    aA + w->acmax },
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

/* the rail runs of one rim edge: cut, sort, merge within 0.05, keep runs over 0.15 */
static int ref_rim_runs(const vec2 va, const vec2 vb, const ref_window *win, int nr_win, float (*runs)[2])
{
    vec2 dir = { vb[0] - va[0], vb[1] - va[1] };
    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1]);
    float c0[4], c1[4];
    int nc = 0, nr = 0;

    for (int c = 0; c < nr_win; c++) {
        float t0, t1;

        if (!ref_edge_cut(va, vb, &win[c], &t0, &t1))   continue;
        c0[nc] = t0 * len;
        c1[nc] = t1 * len;
        nc++;
    }

    for (int i = 1; i < nc; i++)
        for (int j = i; j > 0 && c0[j] < c0[j - 1]; j--) {
            float t;

            t = c0[j]; c0[j] = c0[j - 1]; c0[j - 1] = t;
            t = c1[j]; c1[j] = c1[j - 1]; c1[j - 1] = t;
        }

    int nm = 0;
    for (int i = 0; i < nc; i++)
        if (nm && c0[i] <= c1[nm - 1] + 0.05f) {
            c1[nm - 1] = fmaxf(c1[nm - 1], c1[i]);
        } else {
            c0[nm] = c0[i];
            c1[nm] = c1[i];
            nm++;
        }
    nc = nm;

    float at = 0.0f;
    for (int k = 0; k <= nc; k++) {
        float end = k < nc ? c0[k] : len;

        if (end - at > 0.15f) {
            runs[nr][0] = at;
            runs[nr][1] = end;
            nr++;
        }
        if (k < nc) at = c1[k];
    }

    return nr;
}

/* a window from vertex 2f of the r0-gon toward vertex 2f + 1 of the r1-gon */
static void ref_window_calc(ref_window *w, int n, float r0, float r1, int f)
{
    vec2 B;

    ref_ngon_vertex(2 * f, n, r0, w->A);
    ref_ngon_vertex(2 * f + 1, n, r1, B);

    w->dir[0] = B[0] - w->A[0];
    w->dir[1] = B[1] - w->A[1];

    float len = sqrtf(w->dir[0] * w->dir[0] + w->dir[1] * w->dir[1]);

    w->dir[0] /= len;
    w->dir[1] /= len;
    w->in[0] = -w->dir[1];
    w->in[1] = w->dir[0];
}

/* the same window as a region: the sentinels become open ends */
static void ref_window_region(const ref_window *w, pcg_region *r)
{
    *r = (pcg_region){
        .frame  = { .org = { w->A[0], 0.0f, w->A[1] }, .dir = { w->dir[0], w->dir[1] } },
        .along  = { .lo = w->al0, .hi = w->al1, .open_lo = w->al0 <= -1e9f, .open_hi = w->al1 >= 1e9f },
        .across = { .hi = w->acmax, .open_lo = true },
    };
}

static int span_region_test(void)
{
    const int n = 12;
    const float r = 9.0f;
    /* closed, open below, open above */
    const float al[3][2] = { { -0.4f, 1.1f }, { -1e9f, 2.6f }, { 3.5f, 1e9f } };
    int cuts = 0;

    for (int f = 0; f < n / 2; f++)
        for (int k = 0; k < 3; k++) {
            ref_window w;
            pcg_region reg;

            ref_window_calc(&w, n, r, r - 0.6f, f);
            w.al0 = al[k][0];
            w.al1 = al[k][1];
            w.acmax = 1.83f;
            ref_window_region(&w, &reg);

            for (int i = 0; i < n; i++) {
                vec2 va, vb;
                float t0, t1;
                pcg_span cut;

                ref_ngon_vertex(i, n, r, va);
                ref_ngon_vertex(i + 1, n, r, vb);

                float len = sqrtf((vb[0] - va[0]) * (vb[0] - va[0]) + (vb[1] - va[1]) * (vb[1] - va[1]));
                bool rcut = ref_edge_cut(va, vb, &w, &t0, &t1);
                bool pcut = pcg_region_cut(&reg, va, vb, 1e-3f, &cut);

                if (rcut != pcut)                                       return EXIT_FAILURE;
                if (pcut && (cut.lo != t0 * len || cut.hi != t1 * len)) return EXIT_FAILURE;
                cuts += pcut;
            }
        }
    if (!cuts)  return EXIT_FAILURE;

    /* the across range in a mirrored frame reaches the other way */
    pcg_region reg = {
        .frame  = { .dir = { 1.0f, 0.0f }, .mirror = true },
        .along  = { .lo = 1.0f, .hi = 2.0f },
        .across = { .lo = 0.0f, .hi = 1.0f },
    };
    pcg_span cut;

    if (pcg_region_cut(&reg, (vec2){ 0.0f, 0.5f }, (vec2){ 4.0f, 0.5f }, 1e-3f, &cut))      return EXIT_FAILURE;
    if (!pcg_region_cut(&reg, (vec2){ 0.0f, -0.5f }, (vec2){ 4.0f, -0.5f }, 1e-3f, &cut))   return EXIT_FAILURE;
    if (!span_eq(&cut, 1.0f, 2.0f))                                                         return EXIT_FAILURE;

    /* a crossing shorter than the minimum fraction doesn't count */
    reg.along = (pcg_range){ .lo = 1.0f, .hi = 1.002f };
    if (pcg_region_cut(&reg, (vec2){ 0.0f, -0.5f }, (vec2){ 4.0f, -0.5f }, 1e-3f, &cut))    return EXIT_FAILURE;

    /* a bounded end must be finite, an open one is never read */
    pcg_region regs[2] = { reg, reg };
    if (failed(pcg_regions_check(regs, 2)) || failed(pcg_regions_check(nullptr, 0)))        return EXIT_FAILURE;
    if (!failed(pcg_regions_check(nullptr, 1)))                                             return EXIT_FAILURE;
    regs[1].along.hi = INFINITY;
    if (!failed(pcg_regions_check(regs, 2)))                                                return EXIT_FAILURE;
    regs[1].along.open_hi = true;
    if (failed(pcg_regions_check(regs, 2)))                                                 return EXIT_FAILURE;
    regs[1].across.lo = NAN;
    if (!failed(pcg_regions_check(regs, 2)))                                                return EXIT_FAILURE;
    regs[1].across.open_lo = true;
    if (failed(pcg_regions_check(regs, 2)))                                                 return EXIT_FAILURE;
    regs[0].along.lo = -INFINITY;
    if (!failed(pcg_regions_check(regs, 2)))                                                return EXIT_FAILURE;

    /* and so must the frame, or its half-planes take every segment whole */
    regs[0] = regs[1];
    if (failed(pcg_regions_check(regs, 2)))                                                 return EXIT_FAILURE;
    for (size_t i = 0; i < array_size(bad_frames); i++) {
        regs[1].frame = bad_frames[i];
        if (!failed_inval(pcg_regions_check(regs, 2)))                                      return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int span_segment_runs_test(void)
{
    const int n = 12;
    const float r = 9.0f;
    const pcg_span_tol tol = { .min_frac = 1e-3f, .merge = 0.05f, .min_run = 0.15f };
    int split = 0;

    for (int f = 0; f + 1 < n / 2; f++) {
        /* an arriving window, open beyond the landing, and a departing one, open behind it */
        ref_window w[2];
        pcg_region reg[2];

        ref_window_calc(&w[0], n, r, r - 0.6f, f);
        w[0].al0 = 2.2f;
        w[0].al1 = 1e9f;
        w[0].acmax = 1.83f;
        ref_window_calc(&w[1], n, r, r - 0.6f, f + 1);
        w[1].al0 = -1e9f;
        w[1].al1 = 1.4f;
        w[1].acmax = 1.83f;
        ref_window_region(&w[0], &reg[0]);
        ref_window_region(&w[1], &reg[1]);

        for (int i = 0; i < n; i++) {
            vec2 va, vb;
            float ref[3][2];
            pcg_span out[3];

            ref_ngon_vertex(i, n, r, va);
            ref_ngon_vertex(i + 1, n, r, vb);

            int nref = ref_rim_runs(va, vb, w, 2, ref);
            size_t nr = CRES_RET(pcg_segment_runs(va, vb, reg, 2, &tol, out, 3), return EXIT_FAILURE);

            if (nr != (size_t)nref)                                     return EXIT_FAILURE;
            for (size_t k = 0; k < nr; k++)
                if (out[k].lo != ref[k][0] || out[k].hi != ref[k][1])   return EXIT_FAILURE;
            split += nr != 1;
        }
    }
    if (!split) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int span_openings_test(void)
{
    const pcg_opening ok[] = { { .at = 1.0f, .w = 1.0f, .h = 2.1f }, { .at = 2.5f, .w = 1.0f, .h = 2.1f } };

    if (failed(pcg_openings_check(ok, 2, 3.0f)))        return EXIT_FAILURE;
    if (failed(pcg_openings_check(nullptr, 0, 3.0f)))   return EXIT_FAILURE;

    pcg_span s = pcg_opening_span(&ok[1]);
    if (!span_eq(&s, 2.0f, 3.0f))                                   return EXIT_FAILURE;

    /* past the end, before the start, overlapping, out of order, no width */
    if (!failed(pcg_openings_check(ok, 2, 2.9f)))                                       return EXIT_FAILURE;
    if (!failed(pcg_openings_check(&(pcg_opening){ .at = 0.4f, .w = 1.0f }, 1, 3.0f)))  return EXIT_FAILURE;

    const pcg_opening overlap[] = { { .at = 1.0f, .w = 1.0f }, { .at = 1.4f, .w = 0.2f } };
    if (!failed(pcg_openings_check(overlap, 2, 3.0f)))  return EXIT_FAILURE;

    const pcg_opening order[] = { ok[1], ok[0] };
    if (!failed(pcg_openings_check(order, 2, 3.0f)))                        return EXIT_FAILURE;
    if (!failed(pcg_openings_check(&(pcg_opening){ .at = 1.0f }, 1, 3.0f))) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Sink
 ****************************************************************************/

/* fake materials: the emitting layer reads only their pointers and flags */
static const pcg_material concrete = { .name = "concrete", .uv_mode = PCG_UV_METERS };
static const pcg_material wood = { .name = "wood", .uv_mode = PCG_UV_METERS };
static const pcg_material bronze = { .name = "bronze", .uv_mode = PCG_UV_METERS, .skip_shadow = true };
static const pcg_material decal = { .name = "decal", .uv_mode = PCG_UV_UNIT };
static const pcg_material nameless = { .uv_mode = PCG_UV_METERS };

static bool target_failed(cres(pcg_target) res, cerr_enum code)
{
    return IS_CERR(res) && res.err == code;
}

/* an axis-aligned box [lo, hi] as 6 outward quads: 36 vertices into a reservation */
static void emit_cube(const pcg_target *t, const vec3 lo, const vec3 hi)
{
    float x0 = lo[0], y0 = lo[1], z0 = lo[2], x1 = hi[0], y1 = hi[1], z1 = hi[2];
    const vec4 uv = { 0.0f, 0.0f, 1.0f, 1.0f };
    prim_emit_opts opts = { .mesh = t->mesh, .clockwise = t->clockwise };

    _prim_emit_face((vec3){ x1, y0, z0 }, (vec3){ x1, y0, z1 }, (vec3){ x1, y1, z1 }, (vec3){ x1, y1, z0 }, uv, &opts);
    _prim_emit_face((vec3){ x0, y0, z1 }, (vec3){ x0, y0, z0 }, (vec3){ x0, y1, z0 }, (vec3){ x0, y1, z1 }, uv, &opts);
    _prim_emit_face((vec3){ x0, y1, z0 }, (vec3){ x1, y1, z0 }, (vec3){ x1, y1, z1 }, (vec3){ x0, y1, z1 }, uv, &opts);
    _prim_emit_face((vec3){ x0, y0, z0 }, (vec3){ x0, y0, z1 }, (vec3){ x1, y0, z1 }, (vec3){ x1, y0, z0 }, uv, &opts);
    _prim_emit_face((vec3){ x1, y0, z1 }, (vec3){ x0, y0, z1 }, (vec3){ x0, y1, z1 }, (vec3){ x1, y1, z1 }, uv, &opts);
    _prim_emit_face((vec3){ x0, y0, z0 }, (vec3){ x1, y0, z0 }, (vec3){ x1, y1, z0 }, (vec3){ x0, y1, z0 }, uv, &opts);
}

/* reserve and emit a unit cube at @x for @surf */
static cres(pcg_target) sink_cube(const pcg_ctx *ctx, const pcg_surface *surf, float x)
{
    pcg_target t = CRES_RET(pcg_sink_reserve(ctx, surf, PCG_UV_METERS, 36), return __resp);

    emit_cube(&t, (vec3){ x, 0.0f, 0.0f }, (vec3){ x + 1.0f, 1.0f, 1.0f });

    return cres_val(pcg_target, t);
}

#define SINK_MESHES_MAX 8

/* what pcg_sink_for_each_mesh() reports */
struct sink_walk {
    pcg_key     key[SINK_MESHES_MAX];
    struct mesh *mesh[SINK_MESHES_MAX];
    size_t      nr;
};

static cerr sink_walk_fn(const pcg_key *key, struct mesh *mesh, void *priv)
{
    struct sink_walk *w = priv;

    if (w->nr == SINK_MESHES_MAX)   return CERR_TOO_LARGE;
    w->key[w->nr] = *key;
    w->mesh[w->nr++] = mesh;

    return CERR_OK;
}

/* within 16-bit indices, one index per vertex, tangents for all of them */
static bool sink_mesh_ok(struct mesh *m)
{
    return mesh_nr_vx(m) <= USHRT_MAX && mesh_nr_idx(m) == mesh_nr_vx(m) &&
           mesh_tangent(m) && mesh_attr(m, MESH_TANGENTS)->nr == mesh_nr_vx(m);
}

static int sink_rollover_test(void)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("test"), return EXIT_FAILURE);
    pcg_palette pal = { .mat[PCG_ROLE_STRUCTURE] = &concrete };
    pcg_ctx ctx = { .sink = sink, .palette = &pal, .frame = PCG_FRAME_IDENTITY };
    const pcg_surface surf = { .role = PCG_ROLE_STRUCTURE, .collide = PCG_COLLIDE_SOLID };

    /* 1820 boxes fill 65520 of the 65535 16-bit indices, the 1821st starts a new mesh */
    struct mesh *first = nullptr;
    for (int i = 0; i < 1821; i++) {
        pcg_target t = CRES_RET(sink_cube(&ctx, &surf, (float)i), return EXIT_FAILURE);

        if (!first)                             first = t.mesh;
        if ((i < 1820) != (t.mesh == first))    return EXIT_FAILURE;
    }

    struct sink_walk w = {};
    if (failed(pcg_sink_for_each_mesh(sink, sink_walk_fn, &w)))     return EXIT_FAILURE;
    if (w.nr != 2)                                                  return EXIT_FAILURE;
    if (mesh_nr_vx(w.mesh[0]) != 1820 * 36)                         return EXIT_FAILURE;
    if (mesh_nr_vx(w.mesh[1]) != 36)                                return EXIT_FAILURE;
    if (!sink_mesh_ok(w.mesh[0]) || !sink_mesh_ok(w.mesh[1]))       return EXIT_FAILURE;
    if (strcmp(w.mesh[0]->name, "test/concrete/solid/0.0"))         return EXIT_FAILURE;
    if (strcmp(w.mesh[1]->name, "test/concrete/solid/0.1"))         return EXIT_FAILURE;
    if (w.key[0].mat != &concrete || w.key[1].mat != &concrete)     return EXIT_FAILURE;

    pcg_sink_stats st = pcg_sink_get_stats(sink);
    if (st.nr_models != 2 || st.nr_vertices != 1821 * 36 || st.nr_casters != 2 || st.nr_labels) return EXIT_FAILURE;

    /* reservations that don't divide 65535 evenly roll over before they would cross it */
    ctx.chunk = 1;
    for (int i = 0; i < 70; i++) {
        pcg_target t = CRES_RET(pcg_sink_reserve(&ctx, &surf, PCG_UV_METERS, 1002), return EXIT_FAILURE);

        for (int j = 0; j < 1002 / 36; j++)
            emit_cube(&t, (vec3){ (float)j, 0.0f, 0.0f }, (vec3){ j + 1.0f, 1.0f, 1.0f });
        for (int j = 0; j < (1002 % 36) / 3; j++)
            _prim_emit_triangle3((vec3){ 0, 0, 0 }, (vec3){ 0, 0, 1 }, (vec3){ 1, 0, 0 },
                                 &(prim_emit_opts){ .mesh = t.mesh });
    }

    w = (struct sink_walk){};
    if (failed(pcg_sink_for_each_mesh(sink, sink_walk_fn, &w)))     return EXIT_FAILURE;
    if (w.nr != 4)                                                  return EXIT_FAILURE;
    if (mesh_nr_vx(w.mesh[2]) != 65 * 1002)                         return EXIT_FAILURE;
    if (mesh_nr_vx(w.mesh[3]) != 5 * 1002)                          return EXIT_FAILURE;
    if (!sink_mesh_ok(w.mesh[2]) || !sink_mesh_ok(w.mesh[3]))       return EXIT_FAILURE;
    if (strcmp(w.mesh[3]->name, "test/concrete/solid/1.1"))         return EXIT_FAILURE;

    /* more than a mesh can hold at all */
    if (!target_failed(pcg_sink_reserve(&ctx, &surf, PCG_UV_METERS, USHRT_MAX + 1), _CERR_TOO_LARGE))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int sink_key_test(void)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("key"), return EXIT_FAILURE);
    pcg_palette pal = {
        .mat[PCG_ROLE_STRUCTURE]    = &concrete,
        .mat[PCG_ROLE_FLOOR]        = &concrete,
        .mat[PCG_ROLE_RAIL]         = &wood,
        .mat[PCG_ROLE_METAL]        = &bronze,
    };
    pcg_ctx ctx = { .sink = sink, .palette = &pal, .frame = PCG_FRAME_IDENTITY };
    const pcg_surface slab_s = { .role = PCG_ROLE_STRUCTURE, .collide = PCG_COLLIDE_SOLID };
    const pcg_surface deck_s = { .role = PCG_ROLE_FLOOR, .collide = PCG_COLLIDE_SOLID };
    const pcg_surface pick_s = { .mat = &concrete, .role = PCG_ROLE_RAIL, .collide = PCG_COLLIDE_SOLID };
    const pcg_surface rail_s = { .role = PCG_ROLE_RAIL, .collide = PCG_COLLIDE_SOLID };
    const pcg_surface hazard_s = { .role = PCG_ROLE_RAIL, .collide = PCG_COLLIDE_HAZARD };
    const pcg_surface trim_s = { .role = PCG_ROLE_METAL, .collide = PCG_COLLIDE_NONE };

    /* two roles on one material and an explicit pick of it share one bucket */
    pcg_target slab = CRES_RET(sink_cube(&ctx, &slab_s, 0.0f), return EXIT_FAILURE);
    pcg_target deck = CRES_RET(sink_cube(&ctx, &deck_s, 1.0f), return EXIT_FAILURE);
    pcg_target pick = CRES_RET(sink_cube(&ctx, &pick_s, 2.0f), return EXIT_FAILURE);
    if (deck.mesh != slab.mesh || pick.mesh != slab.mesh)   return EXIT_FAILURE;

    /* another material, another collide class, another chunk: three more */
    pcg_target rail = CRES_RET(sink_cube(&ctx, &rail_s, 0.0f), return EXIT_FAILURE);
    pcg_target hazard = CRES_RET(sink_cube(&ctx, &hazard_s, 0.0f), return EXIT_FAILURE);
    ctx.chunk = 7;
    pcg_target level = CRES_RET(sink_cube(&ctx, &deck_s, 0.0f), return EXIT_FAILURE);
    if (rail.mesh == slab.mesh || hazard.mesh == rail.mesh || level.mesh == slab.mesh)  return EXIT_FAILURE;

    /* a shadowless material's bucket */
    CRES_RET(sink_cube(&ctx, &trim_s, 0.0f), return EXIT_FAILURE);

    struct sink_walk w = {};
    if (failed(pcg_sink_for_each_mesh(sink, sink_walk_fn, &w)))     return EXIT_FAILURE;
    if (w.nr != 5)                                                  return EXIT_FAILURE;
    if (mesh_nr_vx(w.mesh[0]) != 3 * 36)                            return EXIT_FAILURE;

    const pcg_key keys[] = {
        { .mat = &concrete, .chunk = 0, .collide = PCG_COLLIDE_SOLID },
        { .mat = &wood, .chunk = 0, .collide = PCG_COLLIDE_SOLID },
        { .mat = &wood, .chunk = 0, .collide = PCG_COLLIDE_HAZARD },
        { .mat = &concrete, .chunk = 7, .collide = PCG_COLLIDE_SOLID },
        { .mat = &bronze, .chunk = 7, .collide = PCG_COLLIDE_NONE },
    };
    for (size_t i = 0; i < array_size(keys); i++)
        if (w.key[i].mat != keys[i].mat || w.key[i].chunk != keys[i].chunk || w.key[i].collide != keys[i].collide)
            return EXIT_FAILURE;
    if (strcmp(w.mesh[4]->name, "key/bronze/none/7.0"))             return EXIT_FAILURE;

    pcg_sink_stats st = pcg_sink_get_stats(sink);
    if (st.nr_models != 5 || st.nr_vertices != 7 * 36 || st.nr_casters != 4)    return EXIT_FAILURE;

    /* the target carries the surface's UV swap and the frame's mirror */
    pcg_surface swap_s = deck_s;
    swap_s.uv_swap = true;
    pcg_target t = CRES_RET(sink_cube(&ctx, &swap_s, 0.0f), return EXIT_FAILURE);
    if (!t.uv_swap || t.clockwise)                                  return EXIT_FAILURE;
    ctx.frame.mirror = true;
    t = CRES_RET(sink_cube(&ctx, &deck_s, 0.0f), return EXIT_FAILURE);
    if (t.uv_swap || !t.clockwise)                                  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int sink_resolve_test(void)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("resolve"), return EXIT_FAILURE);
    pcg_palette pal = { .mat[PCG_ROLE_STRUCTURE] = &concrete, .mat[PCG_ROLE_SIGN] = &decal };
    pcg_ctx ctx = { .sink = sink, .palette = &pal, .frame = PCG_FRAME_IDENTITY };
    const cerr_enum inval = _CERR_INVALID_ARGUMENTS;
    const pcg_surface structure = { .role = PCG_ROLE_STRUCTURE };
    const pcg_surface carpet = { .role = PCG_ROLE_CARPET };
    const pcg_surface sign = { .role = PCG_ROLE_SIGN };
    const pcg_surface skipped = { .role = PCG_ROLE_STRUCTURE, .skip = true };
    const pcg_surface bad_role = { .role = PCG_ROLE_NR };
    const pcg_surface bad_collide = { .collide = PCG_COLLIDE_NR };
    const pcg_surface unnamed = { .mat = &nameless };

    /* no material: neither an explicit one nor the role's, or no palette at all */
    if (!target_failed(pcg_sink_reserve(&ctx, &carpet, PCG_UV_METERS, 36), inval))  return EXIT_FAILURE;
    pcg_ctx bare = ctx;
    bare.palette = nullptr;
    if (!target_failed(pcg_sink_reserve(&bare, &structure, PCG_UV_METERS, 36), inval))  return EXIT_FAILURE;

    /* the UV contract, both ways */
    if (!target_failed(pcg_sink_reserve(&ctx, &sign, PCG_UV_METERS, 36), inval))    return EXIT_FAILURE;
    if (!target_failed(pcg_sink_reserve(&ctx, &structure, PCG_UV_UNIT, 36), inval)) return EXIT_FAILURE;

    /* skipped, out of range, empty, nameless */
    if (!target_failed(pcg_sink_reserve(&ctx, &skipped, PCG_UV_METERS, 36), inval))     return EXIT_FAILURE;
    if (!target_failed(pcg_sink_reserve(&ctx, &bad_role, PCG_UV_METERS, 36), inval))    return EXIT_FAILURE;
    if (!target_failed(pcg_sink_reserve(&ctx, &bad_collide, PCG_UV_METERS, 36), inval)) return EXIT_FAILURE;
    if (!target_failed(pcg_sink_reserve(&ctx, &structure, PCG_UV_NR, 36), inval))       return EXIT_FAILURE;
    if (!target_failed(pcg_sink_reserve(&ctx, &structure, PCG_UV_METERS, 0), inval))    return EXIT_FAILURE;
    if (!target_failed(pcg_sink_reserve(&ctx, &unnamed, PCG_UV_METERS, 36), inval))     return EXIT_FAILURE;

    /* none of them made a bucket */
    pcg_sink_stats st = pcg_sink_get_stats(sink);
    if (st.nr_models || st.nr_vertices)                             return EXIT_FAILURE;

    /* an explicit material needs no palette */
    pcg_surface picked = structure;
    picked.mat = &wood;
    CRES_RET(sink_cube(&bare, &picked, 0.0f), return EXIT_FAILURE);

    return EXIT_SUCCESS;
}

/* the first label pcg_sink_for_each_label() reports */
static cerr sink_label_fn(const pcg_label *label, void *priv)
{
    pcg_label *out = priv;

    if (!out->text)     *out = *label;

    return CERR_OK;
}

static int sink_label_test(void)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("label"), return EXIT_FAILURE);
    pcg_palette pal = { .mat[PCG_ROLE_SIGN] = &decal, .mat[PCG_ROLE_METAL] = &bronze };
    pcg_ctx ctx = { .sink = sink, .palette = &pal, .chunk = 3 };
    const pcg_surface plate = { .role = PCG_ROLE_SIGN };
    const pcg_surface metal = { .role = PCG_ROLE_METAL };
    char text[] = "1.07";
    pcg_frame at;

    test_frame(&ctx.frame, 0.6f, false, 10.0f, 12.0f, -4.0f);
    test_frame(&at, 1.2f, true, 2.0f, 1.6f, 0.5f);

    if (failed(pcg_sink_label(&ctx, &plate, &at, 0.3f, text)))      return EXIT_FAILURE;
    text[0] = 'X';

    pcg_label l = {};
    if (failed(pcg_sink_for_each_label(sink, sink_label_fn, &l)))                   return EXIT_FAILURE;
    if (strcmp(l.text, "1.07") || l.h != 0.3f || l.chunk != 3 || l.mat != &decal)   return EXIT_FAILURE;

    pcg_frame f;
    pcg_frame_compose(&ctx.frame, &at, &f);
    if (!vec3_near(l.frame.org, f.org, 0.0f) || l.frame.dir[0] != f.dir[0] || !l.frame.mirror)  return EXIT_FAILURE;

    /* a child context composes the same way */
    pcg_ctx child = pcg_ctx_at(&ctx, &at);
    if (!vec3_near(child.frame.org, f.org, 0.0f) || child.sink != sink || child.chunk != 3) return EXIT_FAILURE;

    /* plates take unit UVs, have a height and some text */
    if (!failed(pcg_sink_label(&ctx, &metal, &at, 0.3f, "x")))      return EXIT_FAILURE;
    if (!failed(pcg_sink_label(&ctx, &plate, &at, 0.0f, "x")))      return EXIT_FAILURE;
    if (!failed(pcg_sink_label(&ctx, &plate, &at, 0.3f, "")))       return EXIT_FAILURE;
    if (!failed(pcg_sink_label(&ctx, &plate, &at, 0.3f, nullptr)))  return EXIT_FAILURE;

    /* the label alone is a model, its atlas's */
    pcg_sink_stats st = pcg_sink_get_stats(sink);
    if (st.nr_labels != 1 || st.nr_models != 1 || st.nr_vertices != 6)  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Shapes
 ****************************************************************************/

/* positions on a 1e-4 grid, so corners shared between faces compare equal */
static int32_t quant(float x)
{
    return (int32_t)lrintf(x * 1e4f);
}

static size_t walk_nr_vx(const struct sink_walk *w)
{
    size_t nr = 0;

    for (size_t i = 0; i < w->nr; i++)  nr += mesh_nr_vx(w->mesh[i]);

    return nr;
}

typedef struct edge_key {
    int32_t p[3];
    int32_t q[3];
} edge_key;

static int edge_key_cmp(const void *a, const void *b)
{
    return memcmp(a, b, sizeof(edge_key));
}

/* closed: every directed edge of every triangle has its reverse in another one */
static bool walk_closed(const struct sink_walk *w)
{
    size_t nr = walk_nr_vx(w), n = 0;
    LOCAL_SET(void, fbuf) = mem_alloc(sizeof(edge_key), .nr = nr ? nr : 1);
    LOCAL_SET(void, rbuf) = mem_alloc(sizeof(edge_key), .nr = nr ? nr : 1);
    edge_key *fwd = fbuf, *rev = rbuf;

    for (size_t i = 0; i < w->nr; i++) {
        struct mesh *m = w->mesh[i];
        float *vx = mesh_vx(m);
        unsigned short *idx = mesh_idx(m);

        for (size_t t = 0; t + 2 < mesh_nr_idx(m); t += 3)
            for (int e = 0; e < 3; e++) {
                const float *p = &vx[idx[t + e] * 3], *q = &vx[idx[t + (e + 1) % 3] * 3];

                for (int k = 0; k < 3; k++) {
                    fwd[n].p[k] = rev[n].q[k] = quant(p[k]);
                    fwd[n].q[k] = rev[n].p[k] = quant(q[k]);
                }
                n++;
            }
    }

    qsort(fwd, n, sizeof(edge_key), edge_key_cmp);
    qsort(rev, n, sizeof(edge_key), edge_key_cmp);

    return n && !memcmp(fwd, rev, n * sizeof(edge_key));
}

/* signed volume: positive when the faces point out of a closed surface */
static double walk_volume(const struct sink_walk *w)
{
    double vol = 0.0;

    for (size_t i = 0; i < w->nr; i++) {
        struct mesh *m = w->mesh[i];
        float *vx = mesh_vx(m);
        unsigned short *idx = mesh_idx(m);

        for (size_t t = 0; t + 2 < mesh_nr_idx(m); t += 3) {
            const float *a = &vx[idx[t] * 3], *b = &vx[idx[t + 1] * 3], *c = &vx[idx[t + 2] * 3];

            vol += (double)a[0] * ((double)b[1] * c[2] - (double)b[2] * c[1]) +
                   (double)a[1] * ((double)b[2] * c[0] - (double)b[0] * c[2]) +
                   (double)a[2] * ((double)b[0] * c[1] - (double)b[1] * c[0]);
        }
    }

    return vol / 6.0;
}

/* the bounding box of every vertex, in @f's local coordinates */
static void walk_aabb(const struct sink_walk *w, const pcg_frame *f, vec3 lo, vec3 hi)
{
    bool first = true;

    for (size_t i = 0; i < w->nr; i++)
        for (size_t v = 0; v < mesh_nr_vx(w->mesh[i]); v++) {
            vec3 p;

            pcg_frame_point_to_local(f, &mesh_vx(w->mesh[i])[v * 3], p);
            for (int k = 0; k < 3; k++) {
                lo[k] = first ? p[k] : fminf(lo[k], p[k]);
                hi[k] = first ? p[k] : fmaxf(hi[k], p[k]);
            }
            first = false;
        }
}

static bool walk_aabb_is(const struct sink_walk *w, const pcg_frame *f, const vec3 lo, const vec3 hi)
{
    vec3 l, h;

    walk_aabb(w, f, l, h);

    return vec3_near(l, lo, 1e-4f) && vec3_near(h, hi, 1e-4f);
}

/* every normal of @m points along @dir (local to @f) */
static bool mesh_normals_along(struct mesh *m, const pcg_frame *f, const vec3 dir)
{
    for (size_t v = 0; v < mesh_nr_vx(m); v++) {
        vec3 n;

        pcg_frame_vector_to_local(f, &mesh_norm(m)[v * 3], n);
        if (!vec3_near(n, dir, 1e-4f))  return false;
    }

    return true;
}

/* the number of vertices of @m whose normal points along @dir (local to @f) */
static size_t mesh_count_normals(struct mesh *m, const pcg_frame *f, const vec3 dir)
{
    size_t nr = 0;

    for (size_t v = 0; v < mesh_nr_vx(m); v++) {
        vec3 n;

        pcg_frame_vector_to_local(f, &mesh_norm(m)[v * 3], n);
        if (vec3_near(n, dir, 1e-4f))   nr++;
    }

    return nr;
}

/* every normal of @m is horizontal */
static bool mesh_normals_level(struct mesh *m)
{
    for (size_t v = 0; v < mesh_nr_vx(m); v++)
        if (!near(mesh_norm(m)[v * 3 + 1], 0.0f, 1e-4f))    return false;

    return true;
}

/* unit tangents perpendicular to the normals, with a handedness */
static bool mesh_tangents_ok(struct mesh *m)
{
    for (size_t v = 0; v < mesh_nr_vx(m); v++) {
        const float *t = &mesh_tangent(m)[v * 4], *n = &mesh_norm(m)[v * 3];

        if (!near(vec3_len(t), 1.0f, 1e-4f))            return false;
        if (!near(vec3_mul_inner(t, n), 0.0f, 1e-4f))   return false;
        if (!near(fabsf(t[3]), 1.0f, 1e-6f))            return false;
    }

    return true;
}

typedef struct tri_key {
    int32_t c[3][5];
} tri_key;

static int tri_key_cmp(const void *a, const void *b)
{
    return memcmp(a, b, sizeof(tri_key));
}

/* @m's triangles with their UVs, each starting at its smallest corner, sorted */
static tri_key *mesh_tri_keys(struct mesh *m)
{
    size_t nr = mesh_nr_vx(m) / 3;
    tri_key *keys = mem_alloc(sizeof(*keys), .nr = nr ? nr : 1);
    unsigned short *idx = mesh_idx(m);

    for (size_t t = 0; t < nr; t++) {
        int32_t c[3][5];

        for (int k = 0; k < 3; k++) {
            unsigned short i = idx[t * 3 + k];

            for (int j = 0; j < 3; j++) c[k][j] = quant(mesh_vx(m)[i * 3 + j]);
            for (int j = 0; j < 2; j++) c[k][3 + j] = quant(mesh_tx(m)[i * 2 + j]);
        }

        int first = 0;
        for (int k = 1; k < 3; k++)
            if (memcmp(c[k], c[first], sizeof(c[k])) < 0)   first = k;
        for (int k = 0; k < 3; k++)                         memcpy(keys[t].c[k], c[(first + k) % 3], sizeof(c[k]));
    }
    qsort(keys, nr, sizeof(*keys), tri_key_cmp);

    return keys;
}

/* the same triangles, wound the same way, with the same UVs, in any order */
static bool mesh_same_tris(struct mesh *a, struct mesh *b)
{
    if (mesh_nr_vx(a) != mesh_nr_vx(b))     return false;

    LOCAL_SET(void, ka) = mesh_tri_keys(a);
    LOCAL_SET(void, kb) = mesh_tri_keys(b);

    return !memcmp(ka, kb, mesh_nr_vx(a) / 3 * sizeof(tri_key));
}

/* the same vertices in the same order, positions within @eps */
static bool mesh_same_vx(struct mesh *a, struct mesh *b, float eps, float uv_eps)
{
    if (mesh_nr_vx(a) != mesh_nr_vx(b))     return false;

    for (size_t v = 0; v < mesh_nr_vx(a); v++) {
        if (!vec3_near(&mesh_vx(a)[v * 3], &mesh_vx(b)[v * 3], eps))        return false;
        if (!near(mesh_tx(a)[v * 2], mesh_tx(b)[v * 2], uv_eps))            return false;
        if (!near(mesh_tx(a)[v * 2 + 1], mesh_tx(b)[v * 2 + 1], uv_eps))    return false;
    }

    return true;
}

/*
 * Reference copies of the original emitters that the shapes replace, with
 * their texture density at 1 repeat per meter: the shapes must emit the
 * same triangles.
 */
static void ref_face(struct mesh *m, vec3 c00, vec3 c10, vec3 c11, vec3 c01, float u0, float v0, float u1, float v1)
{
    prim_emit_opts opts = { .mesh = m };
    vec2 t0[3] = { { u0, v0 }, { u0, v1 }, { u1, v0 } };
    vec2 t1[3] = { { u0, v1 }, { u1, v1 }, { u1, v0 } };

    opts.uv = t0;
    _prim_emit_triangle3(c00, c01, c10, &opts);
    opts.uv = t1;
    _prim_emit_triangle3(c01, c11, c10, &opts);
}

#define REF_PLATE_NO_BOTTOM (1 << 0)
#define REF_PLATE_NO_TOP    (1 << 1)

static cerr ref_plate_emit(struct mesh *m, int sides, float radius, int hole_sides, float hole_r,
                           float y0, float y1, float uv, unsigned int drop)
{
    bool hole = hole_sides >= 3 && hole_r > 0.0f;
    size_t np = sides + (hole ? hole_sides : 0);

    LOCAL_SET(void, ptbuf) = mem_alloc(sizeof(vec2), .nr = np);
    if (!ptbuf) return CERR_NOMEM;

    vec2 *pts = ptbuf;
    for (int j = 0; j < sides; j++)                 ref_ngon_vertex(j, sides, radius, pts[j]);
    for (int j = 0; hole && j < hole_sides; j++)    ref_ngon_vertex(-j, hole_sides, hole_r, pts[sides + j]);

    poly_ring rings[2] = { { 0, (size_t)sides }, { (size_t)sides, (size_t)hole_sides } };
    poly_tris tris = CRES_RET_CERR(poly_triangulate(pts, rings, hole ? 2 : 1));
    LOCAL_SET(void, tris_idx) = tris.idx;

    CRES_RET_CERR(_prim_begin(tris.nr_tris * 6 + np * 6, &(prim_emit_opts){ .mesh = m, .tangents = true }));

    prim_emit_opts po = { .mesh = m };
    for (size_t t = 0; t < tris.nr_tris; t++) {
        const unsigned int *ti = &tris.idx[t * 3];
        vec3 v[3];
        vec2 tuv[3];

        for (int k = 0; k < 3; k++) {
            v[k][0] = pts[ti[k]][0];
            v[k][2] = pts[ti[k]][1];
            tuv[k][0] = v[k][0] * uv;
            tuv[k][1] = v[k][2] * uv;
        }

        if (!(drop & REF_PLATE_NO_BOTTOM)) {
            v[0][1] = v[1][1] = v[2][1] = y0;
            po.uv = tuv;
            _prim_emit_triangle3(v[0], v[1], v[2], &po);
        }

        if (!(drop & REF_PLATE_NO_TOP)) {
            v[0][1] = v[1][1] = v[2][1] = y1;
            vec2 ruv[3] = { { tuv[0][0], tuv[0][1] }, { tuv[2][0], tuv[2][1] }, { tuv[1][0], tuv[1][1] } };
            po.uv = ruv;
            _prim_emit_triangle3(v[0], v[2], v[1], &po);
        }
    }

    for (int ring = 0; ring < (hole ? 2 : 1); ring++)
        for (size_t k = 0; k < rings[ring].nr; k++) {
            const float *a = pts[rings[ring].start + k];
            const float *b = pts[rings[ring].start + (k + 1) % rings[ring].nr];
            float len = sqrtf((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]));

            ref_face(m, (vec3){ a[0], y0, a[1] }, (vec3){ b[0], y0, b[1] },
                     (vec3){ b[0], y1, b[1] }, (vec3){ a[0], y1, a[1] },
                     0.0f, y0 * uv, len * uv, y1 * uv);
        }

    return CERR_OK;
}

static cerr ref_convex_plate_emit(struct mesh *m, const vec2 *pts, int np, float y0, float y1, float uv)
{
    if (np < 3) return CERR_OK;

    CRES_RET_CERR(_prim_begin((np - 2) * 6 + np * 6, &(prim_emit_opts){ .mesh = m, .tangents = true }));

    prim_emit_opts po = { .mesh = m };
    for (int k = 1; k < np - 1; k++) {
        vec3 v[3] = { { pts[0][0], y0, pts[0][1] },
                      { pts[k][0], y0, pts[k][1] },
                      { pts[k + 1][0], y0, pts[k + 1][1] } };
        vec2 tuv[3] = { { pts[0][0] * uv, pts[0][1] * uv },
                        { pts[k][0] * uv, pts[k][1] * uv },
                        { pts[k + 1][0] * uv, pts[k + 1][1] * uv } };

        po.uv = tuv;
        _prim_emit_triangle3(v[0], v[1], v[2], &po);

        v[0][1] = v[1][1] = v[2][1] = y1;
        vec2 ruv[3] = { { tuv[0][0], tuv[0][1] }, { tuv[2][0], tuv[2][1] }, { tuv[1][0], tuv[1][1] } };
        po.uv = ruv;
        _prim_emit_triangle3(v[0], v[2], v[1], &po);
    }

    for (int k = 0; k < np; k++) {
        const float *a = pts[k], *b = pts[(k + 1) % np];
        float len = sqrtf((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]));

        ref_face(m, (vec3){ a[0], y0, a[1] }, (vec3){ b[0], y0, b[1] },
                 (vec3){ b[0], y1, b[1] }, (vec3){ a[0], y1, a[1] },
                 0.0f, y0 * uv, len * uv, y1 * uv);
    }

    return CERR_OK;
}

#define REF_FASCIA_LIP  0.06f
#define REF_FASCIA_TUCK 0.02f

static cerr ref_fascia_seg(struct mesh *m, const vec2 org, const vec2 dir, const vec2 in, float at, float end,
                           float depth, float uv)
{
    float w = REF_FASCIA_LIP + REF_FASCIA_TUCK;
    vec2 o0 = { org[0] - in[0] * REF_FASCIA_TUCK + dir[0] * at,
                org[1] - in[1] * REF_FASCIA_TUCK + dir[1] * at };
    vec2 o1 = { o0[0] + dir[0] * (end - at), o0[1] + dir[1] * (end - at) };
    vec2 w0 = { org[0] + in[0] * REF_FASCIA_LIP + dir[0] * at,
                org[1] + in[1] * REF_FASCIA_LIP + dir[1] * at };
    vec2 w1 = { w0[0] + dir[0] * (end - at), w0[1] + dir[1] * (end - at) };

    CRES_RET_CERR(_prim_begin(36, &(prim_emit_opts){ .mesh = m, .tangents = true }));

    ref_face(m, (vec3){ w1[0], -depth, w1[1] }, (vec3){ w0[0], -depth, w0[1] },
             (vec3){ w0[0], 0.0f, w0[1] }, (vec3){ w1[0], 0.0f, w1[1] },
             at * uv, -depth * uv, end * uv, 0.0f);
    ref_face(m, (vec3){ o0[0], -depth, o0[1] }, (vec3){ o1[0], -depth, o1[1] },
             (vec3){ o1[0], 0.0f, o1[1] }, (vec3){ o0[0], 0.0f, o0[1] },
             at * uv, -depth * uv, end * uv, 0.0f);
    ref_face(m, (vec3){ o0[0], -depth, o0[1] }, (vec3){ w0[0], -depth, w0[1] },
             (vec3){ w1[0], -depth, w1[1] }, (vec3){ o1[0], -depth, o1[1] },
             at * uv, 0.0f, end * uv, w * uv);
    ref_face(m, (vec3){ o0[0], 0.0f, o0[1] }, (vec3){ o1[0], 0.0f, o1[1] },
             (vec3){ w1[0], 0.0f, w1[1] }, (vec3){ w0[0], 0.0f, w0[1] },
             at * uv, 0.0f, end * uv, w * uv);
    ref_face(m, (vec3){ o0[0], -depth, o0[1] }, (vec3){ o0[0], 0.0f, o0[1] },
             (vec3){ w0[0], 0.0f, w0[1] }, (vec3){ w0[0], -depth, w0[1] },
             0.0f, -depth * uv, w * uv, 0.0f);
    ref_face(m, (vec3){ w1[0], -depth, w1[1] }, (vec3){ w1[0], 0.0f, w1[1] },
             (vec3){ o1[0], 0.0f, o1[1] }, (vec3){ o1[0], -depth, o1[1] },
             0.0f, -depth * uv, w * uv, 0.0f);

    return CERR_OK;
}

static cerr ref_glass_emit(struct mesh *m, const vec2 org, const vec2 dir, float len, float y0, float y1,
                           float gap_at, float gap_w, float gap_h, float uv)
{
    float seg[3][3] = { { 0.0f, len, y0 } };
    int nseg = 1;

    if (gap_w > 0.0f) {
        seg[0][1] = gap_at - gap_w * 0.5f;
        seg[1][0] = gap_at + gap_w * 0.5f;
        seg[1][1] = len;
        seg[1][2] = y0;
        nseg = 2;
        if (gap_h > 0.0f && gap_h < y1) {
            seg[2][0] = seg[0][1];
            seg[2][1] = seg[1][0];
            seg[2][2] = gap_h;
            nseg = 3;
        }
    }

    CRES_RET_CERR(_prim_begin(nseg * 12, &(prim_emit_opts){ .mesh = m, .tangents = true }));

    for (int i = 0; i < nseg; i++) {
        float a0 = seg[i][0], a1 = seg[i][1], ylo = seg[i][2];

        if (a1 - a0 < 0.01f)    continue;

#define GLPT(_a, _y) (vec3){ org[0] + (_a) * dir[0], (_y), org[1] + (_a) * dir[1] }
        ref_face(m, GLPT(a0, ylo), GLPT(a1, ylo), GLPT(a1, y1), GLPT(a0, y1),
                 a0 * uv, ylo * uv, a1 * uv, y1 * uv);
        ref_face(m, GLPT(a1, ylo), GLPT(a0, ylo), GLPT(a0, y1), GLPT(a1, y1),
                 -a1 * uv, ylo * uv, -a0 * uv, y1 * uv);
#undef GLPT
    }

    return CERR_OK;
}

/* fake finishes for the shape tests, one per face group so each gets its own bucket */
static const pcg_material top_mat = { .name = "top", .uv_mode = PCG_UV_METERS };
static const pcg_material bottom_mat = { .name = "bottom", .uv_mode = PCG_UV_METERS };
static const pcg_material edge_mat = { .name = "edge", .uv_mode = PCG_UV_METERS };

/* a hexagonal ring plate of the reference layout's shape: outer ring, then the hole reversed */
static void ring_plate_pts(vec2 *pts, int n, float r, float hole_r)
{
    for (int j = 0; j < n; j++) ref_ngon_vertex(j, n, r, pts[j]);
    for (int j = 0; j < n; j++) ref_ngon_vertex(-j, n, hole_r, pts[n + j]);
}

/* area of a regular n-gon of circumradius r */
static double ngon_area(int n, double r)
{
    return 0.5 * n * r * r * sin(2.0 * M_PI / n);
}

static int shape_plate_test(void)
{
    const int n = 6;
    const float r = 9.0f, hr = 4.0f, y0 = -0.3f, y1 = 0.0f;
    vec2 pts[12];
    const poly_ring rings[2] = { { 0, 6 }, { 6, 6 } };

    ring_plate_pts(pts, n, r, hr);

    /* the original plate, bit for bit, when every face group wears the same material */
    {
        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("plate"), return EXIT_FAILURE);
        pcg_ctx ctx = { .sink = sink, .frame = PCG_FRAME_IDENTITY };
        const pcg_surface s = { .mat = &concrete, .collide = PCG_COLLIDE_SOLID };

        CERR_RET(pcg_plate(&ctx, .pts = pts, .nr_pts = 12, .rings = rings, .nr_rings = 2, .y0 = y0, .y1 = y1,
                           .top = s, .bottom = s, .edge = s),
                 return EXIT_FAILURE);

        struct sink_walk w = {};
        if (failed(pcg_sink_for_each_mesh(sink, sink_walk_fn, &w)) || w.nr != 1)    return EXIT_FAILURE;

        LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref"), return EXIT_FAILURE);
        if (failed(ref_plate_emit(ref, n, r, n, hr, y0, y1, 1.0f, 0)))      return EXIT_FAILURE;
        if (!mesh_same_tris(w.mesh[0], ref))                                return EXIT_FAILURE;

        if (!walk_closed(&w))                                               return EXIT_FAILURE;
        double vol = (ngon_area(n, r) - ngon_area(n, hr)) * (y1 - y0);
        if (fabs(walk_volume(&w) - vol) > 1e-3 * vol)                       return EXIT_FAILURE;
    }

    /* a yawed frame, a hole wound like the outer ring, a separate bucket per face group */
    {
        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("plate"), return EXIT_FAILURE);
        pcg_ctx ctx = { .sink = sink };
        vec2 same[12];

        test_frame(&ctx.frame, 0.8f, false, 30.0f, 6.0f, -12.0f);
        for (int j = 0; j < n; j++) {
            ref_ngon_vertex(j, n, r, same[j]);
            ref_ngon_vertex(j, n, hr, same[n + j]);
        }

        CERR_RET(pcg_plate(&ctx, .pts = same, .nr_pts = 12, .rings = rings, .nr_rings = 2, .y0 = y0, .y1 = y1,
                           .top = { .mat = &top_mat, .uv_swap = true }, .bottom = { .mat = &bottom_mat },
                           .edge = { .mat = &edge_mat }),
                 return EXIT_FAILURE);

        struct sink_walk w = {};
        if (failed(pcg_sink_for_each_mesh(sink, sink_walk_fn, &w)) || w.nr != 3)                    return EXIT_FAILURE;
        if (w.key[0].mat != &bottom_mat || w.key[1].mat != &top_mat || w.key[2].mat != &edge_mat)   return EXIT_FAILURE;

        /* 12 points - 2 + 2 for the bridged hole: 12 triangles; 12 edge quads */
        if (mesh_nr_vx(w.mesh[0]) != 12 * 3 || mesh_nr_vx(w.mesh[1]) != 12 * 3)         return EXIT_FAILURE;
        if (mesh_nr_vx(w.mesh[2]) != 12 * 6)                                            return EXIT_FAILURE;
        if (!mesh_normals_along(w.mesh[0], &ctx.frame, (vec3){ 0.0f, -1.0f, 0.0f }))    return EXIT_FAILURE;
        if (!mesh_normals_along(w.mesh[1], &ctx.frame, (vec3){ 0.0f, 1.0f, 0.0f }))     return EXIT_FAILURE;
        if (!mesh_normals_level(w.mesh[2]))                                             return EXIT_FAILURE;
        for (size_t i = 0; i < w.nr; i++)
            if (!mesh_tangents_ok(w.mesh[i]))                                           return EXIT_FAILURE;

        /* tops tile in sink space, turned a quarter by the swap; bottoms aren't turned */
        for (size_t v = 0; v < mesh_nr_vx(w.mesh[1]); v++) {
            const float *p = &mesh_vx(w.mesh[1])[v * 3], *t = &mesh_tx(w.mesh[1])[v * 2];

            if (t[0] != p[2] || t[1] != p[0])   return EXIT_FAILURE;
        }
        for (size_t v = 0; v < mesh_nr_vx(w.mesh[0]); v++) {
            const float *p = &mesh_vx(w.mesh[0])[v * 3], *t = &mesh_tx(w.mesh[0])[v * 2];

            if (t[0] != p[0] || t[1] != p[2])   return EXIT_FAILURE;
        }

        if (!walk_closed(&w))                                                       return EXIT_FAILURE;
        double vol = (ngon_area(n, r) - ngon_area(n, hr)) * (y1 - y0);
        if (fabs(walk_volume(&w) - vol) > 1e-3 * vol)                               return EXIT_FAILURE;

        vec2 lo, hi;
        poly2d_bbox(same, n, lo, hi);
        if (!walk_aabb_is(&w, &ctx.frame, (vec3){ lo[0], y0, lo[1] }, (vec3){ hi[0], y1, hi[1] }))  return EXIT_FAILURE;
    }

    /* bad plates */
    {
        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("plate"), return EXIT_FAILURE);
        pcg_ctx ctx = { .sink = sink, .frame = PCG_FRAME_IDENTITY };
        const pcg_surface s = { .mat = &concrete };
        const poly_ring past = { 8, 6 };

        if (!failed(pcg_plate(&ctx, .pts = pts, .nr_pts = 2, .top = s, .bottom = s, .edge = s)))    return EXIT_FAILURE;
        if (!failed(pcg_plate(&ctx, .pts = pts, .nr_pts = 6, .y0 = 1.0f, .top = s, .bottom = s, .edge = s)))
            return EXIT_FAILURE;
        if (!failed(pcg_plate(&ctx, .pts = pts, .nr_pts = 12, .rings = &past, .nr_rings = 1,
                              .top = s, .bottom = s, .edge = s)))
            return EXIT_FAILURE;
        if (!failed(pcg_plate(&ctx, .pts = pts, .nr_pts = 12, .rings = rings, .nr_rings = 2, .fan = true,
                              .top = s, .bottom = s, .edge = s)))
            return EXIT_FAILURE;
        if (pcg_sink_get_stats(sink).nr_models)     return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int shape_plate_skip_test(void)
{
    const int n = 6;
    vec2 pts[12];
    const poly_ring rings[2] = { { 0, 6 }, { 6, 6 } };
    const pcg_material *mats[PCG_PLATE_NR_SURF] = {
        [PCG_PLATE_TOP] = &top_mat, [PCG_PLATE_BOTTOM] = &bottom_mat, [PCG_PLATE_EDGE] = &edge_mat,
    };
    const size_t nr_vx[PCG_PLATE_NR_SURF] = {
        [PCG_PLATE_TOP] = 12 * 3, [PCG_PLATE_BOTTOM] = 12 * 3, [PCG_PLATE_EDGE] = 12 * 6,
    };

    ring_plate_pts(pts, n, 9.0f, 4.0f);

    /* skipping a surface drops exactly its face group */
    for (int skip = 0; skip < PCG_PLATE_NR_SURF; skip++) {
        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("skip"), return EXIT_FAILURE);
        pcg_ctx ctx = { .sink = sink, .frame = PCG_FRAME_IDENTITY };
        pcg_plate_desc d = { .pts = pts, .nr_pts = 12, .rings = rings, .nr_rings = 2, .y0 = -0.3f, .y1 = 0.0f };

        for (int s = 0; s < PCG_PLATE_NR_SURF; s++) d.surf[s] = (pcg_surface){ .mat = mats[s], .skip = s == skip };
        if (failed(_pcg_plate(&ctx, &d)))           return EXIT_FAILURE;

        struct sink_walk w = {};
        if (failed(pcg_sink_for_each_mesh(sink, sink_walk_fn, &w)) || w.nr != 2)    return EXIT_FAILURE;
        for (size_t i = 0; i < w.nr; i++) {
            int s = w.key[i].mat == &top_mat ? PCG_PLATE_TOP :
                    w.key[i].mat == &bottom_mat ? PCG_PLATE_BOTTOM : PCG_PLATE_EDGE;

            if (s == skip || mesh_nr_vx(w.mesh[i]) != nr_vx[s]) return EXIT_FAILURE;
        }
    }

    /* without its top, a slab under a deck is the original plate that leaves its top out */
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("skip"), return EXIT_FAILURE);
    pcg_ctx ctx = { .sink = sink, .frame = PCG_FRAME_IDENTITY };
    const pcg_surface s = { .mat = &concrete };
    CERR_RET(pcg_plate(&ctx, .pts = pts, .nr_pts = 12, .rings = rings, .nr_rings = 2, .y0 = -0.3f, .y1 = 0.0f,
                       .top = { .skip = true }, .bottom = s, .edge = s),
             return EXIT_FAILURE);

    struct sink_walk w = {};
    if (failed(pcg_sink_for_each_mesh(sink, sink_walk_fn, &w)) || w.nr != 1)    return EXIT_FAILURE;

    LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref"), return EXIT_FAILURE);
    if (failed(ref_plate_emit(ref, n, 9.0f, n, 4.0f, -0.3f, 0.0f, 1.0f, REF_PLATE_NO_TOP))) return EXIT_FAILURE;
    if (!mesh_same_tris(w.mesh[0], ref))                                                    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int shape_plate_fan_test(void)
{
    /* a convex outline clipped off a 12-gon, as stair aprons are */
    vec2 pts[16], tmp[16];
    poly2d_regular(tmp, 12, 9.0f, poly2d_phase_edge0_neg_z(12));
    size_t np = CRES_RET(poly2d_clip(tmp, 12, &(poly2d_halfplane){ .n = { 0.6f, 0.8f }, .d = 2.0f }, pts, 16),
                         return EXIT_FAILURE);
    if (np < 5)     return EXIT_FAILURE;

    for (int reversed = 0; reversed < 2; reversed++) {
        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("fan"), return EXIT_FAILURE);
        pcg_ctx ctx = { .sink = sink, .frame = PCG_FRAME_IDENTITY };
        const pcg_surface s = { .mat = &concrete };
        vec2 in[16];

        for (size_t j = 0; j < np; j++) memcpy(in[j], pts[reversed ? np - 1 - j : j], sizeof(vec2));

        CERR_RET(pcg_plate(&ctx, .pts = in, .nr_pts = np, .y0 = 0.0f, .y1 = 0.2f, .fan = true,
                           .top = s, .bottom = s, .edge = s),
                 return EXIT_FAILURE);

        struct sink_walk w = {};
        if (failed(pcg_sink_for_each_mesh(sink, sink_walk_fn, &w)) || w.nr != 1)    return EXIT_FAILURE;
        if (mesh_nr_vx(w.mesh[0]) != (np - 2) * 6 + np * 6)                         return EXIT_FAILURE;
        if (!walk_closed(&w))                                                       return EXIT_FAILURE;

        double area = 0.0;
        for (size_t j = 0; j < np; j++)
            area += (double)pts[j][0] * pts[(j + 1) % np][1] - (double)pts[(j + 1) % np][0] * pts[j][1];
        area *= 0.5;
        if (fabs(walk_volume(&w) - area * 0.2) > 1e-3 * area)                   return EXIT_FAILURE;

        /* the original apron plate, bit for bit */
        if (!reversed) {
            LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref"), return EXIT_FAILURE);
            if (failed(ref_convex_plate_emit(ref, pts, np, 0.0f, 0.2f, 1.0f)))  return EXIT_FAILURE;
            if (!mesh_same_tris(w.mesh[0], ref))                                return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

static int shape_plate_sheet_test(void)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("sheet"), return EXIT_FAILURE);
    pcg_ctx ctx = { .sink = sink, .frame = PCG_FRAME_IDENTITY };
    vec2 pts[8];

    /* a downward-facing sheet: no edges at zero height, only the side that isn't skipped */
    ctx.frame.org[1] = 3.0f;
    poly2d_regular(pts, 8, 5.0f, poly2d_phase_edge0_neg_z(8));
    CERR_RET(pcg_plate(&ctx, .pts = pts, .nr_pts = 8, .y0 = 0.5f, .y1 = 0.5f, .fan = true,
                       .top = { .skip = true }, .bottom = { .mat = &concrete }, .edge = { .mat = &wood }),
             return EXIT_FAILURE);

    struct sink_walk w = {};
    if (failed(pcg_sink_for_each_mesh(sink, sink_walk_fn, &w)) || w.nr != 1)        return EXIT_FAILURE;
    if (w.key[0].mat != &concrete || mesh_nr_vx(w.mesh[0]) != 6 * 3)                return EXIT_FAILURE;
    if (!mesh_normals_along(w.mesh[0], &ctx.frame, (vec3){ 0.0f, -1.0f, 0.0f }))    return EXIT_FAILURE;
    for (size_t v = 0; v < mesh_nr_vx(w.mesh[0]); v++)
        if (mesh_vx(w.mesh[0])[v * 3 + 1] != 3.5f)                                  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int shape_plate_chart_test(void)
{
    const pcg_chart chart = { .org = { 1.0f, -2.0f }, .scale = { 0.5f, 2.0f } };
    vec2 pts[8];

    poly2d_regular(pts, 8, 5.0f, poly2d_phase_edge0_neg_z(8));

    for (int swap = 0; swap < 2; swap++)
        for (int mirror = 0; mirror < 2; mirror++) {
            LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("chart"), return EXIT_FAILURE);
            pcg_ctx ctx = { .sink = sink };
            const pcg_surface caps = { .mat = &concrete, .uv_swap = swap };

            /* the top and bottom chart the local plane through it, wherever the plate stands */
            test_frame(&ctx.frame, 0.7f, mirror, 30.0f, 2.0f, -11.0f);
            CERR_RET(pcg_plate(&ctx, .pts = pts, .nr_pts = 8, .y0 = 0.0f, .y1 = 0.2f, .chart = &chart,
                               .top = caps, .bottom = caps, .edge = { .mat = &wood }),
                     return EXIT_FAILURE);

            struct sink_walk w = {};
            if (failed(pcg_sink_for_each_mesh(sink, sink_walk_fn, &w)) || w.nr != 2)    return EXIT_FAILURE;
            struct mesh *m = w.key[0].mat == &concrete ? w.mesh[0] : w.mesh[1];
            struct mesh *e = w.key[0].mat == &concrete ? w.mesh[1] : w.mesh[0];
            if (mesh_nr_vx(m) != 2 * 6 * 3 || !mesh_tangents_ok(m))             return EXIT_FAILURE;
            for (size_t v = 0; v < mesh_nr_vx(m); v++) {
                vec3 p;

                pcg_frame_point_to_local(&ctx.frame, &mesh_vx(m)[v * 3], p);
                float u = (p[0] - chart.org[0]) * chart.scale[0], t = (p[2] - chart.org[1]) * chart.scale[1];
                if (!near(mesh_tx(m)[v * 2], swap ? t : u, 1e-4f) ||
                    !near(mesh_tx(m)[v * 2 + 1], swap ? u : t, 1e-4f))
                    return EXIT_FAILURE;
            }

            /* the edges keep theirs: U along each edge from 0 */
            if (mesh_nr_vx(e) != 8 * 6)                                         return EXIT_FAILURE;
            for (size_t v = 0; v < mesh_nr_vx(e); v++)
                if (mesh_tx(e)[v * 2] < 0.0f)                                   return EXIT_FAILURE;
        }

    return EXIT_SUCCESS;
}

static int shape_band_test(void)
{
    const float lip = REF_FASCIA_LIP, tuck = REF_FASCIA_TUCK, depth = 1.1f;
    vec2 va, vb, dir, in;

    ref_ngon_vertex(3, 12, 9.0f, va);
    ref_ngon_vertex(4, 12, 9.0f, vb);
    float len = poly2d_edge_frame(va, vb, dir, in);

    /* the original band segment, in the rim edge's frame */
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("band"), return EXIT_FAILURE);
    pcg_ctx ctx = { .sink = sink };
    const pcg_surface s = { .mat = &concrete, .collide = PCG_COLLIDE_SOLID };
    float at = 0.4f, end = len - 0.7f;

    ctx.frame = (pcg_frame){ .org = { va[0], 0.0f, va[1] }, .dir = { dir[0], dir[1] } };
    CERR_RET(pcg_band(&ctx, .lo = { at, -depth, -tuck }, .hi = { end, 0.0f, lip }, .surf = s),
             return EXIT_FAILURE);

    struct sink_walk w = {};
    if (failed(pcg_sink_for_each_mesh(sink, sink_walk_fn, &w)) || w.nr != 1)    return EXIT_FAILURE;

    LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref"), return EXIT_FAILURE);
    if (failed(ref_fascia_seg(ref, va, dir, in, at, end, depth, 1.0f)))     return EXIT_FAILURE;
    if (!mesh_same_vx(w.mesh[0], ref, 1e-5f, 0.0f))                         return EXIT_FAILURE;

    /* closed, outward, the box it was asked for */
    if (!walk_closed(&w) || !mesh_tangents_ok(w.mesh[0]))                   return EXIT_FAILURE;
    double vol = (double)(end - at) * depth * (lip + tuck);
    if (fabs(walk_volume(&w) - vol) > 1e-4 * vol)                                               return EXIT_FAILURE;
    if (!walk_aabb_is(&w, &ctx.frame, (vec3){ at, -depth, -tuck }, (vec3){ end, 0.0f, lip }))   return EXIT_FAILURE;

    /* each skipped face takes its 6 vertices, reversed ranges are swapped back */
    static const vec3 face_dir[6] = {
        [0] = { 1.0f, 0.0f, 0.0f }, [1] = { -1.0f, 0.0f, 0.0f }, [2] = { 0.0f, 1.0f, 0.0f },
        [3] = { 0.0f, -1.0f, 0.0f }, [4] = { 0.0f, 0.0f, 1.0f }, [5] = { 0.0f, 0.0f, -1.0f },
    };
    for (int face = 0; face < 6; face++) {
        LOCAL_SET(pcg_sink, sk) = CRES_RET(pcg_sink_new("band"), return EXIT_FAILURE);
        pcg_ctx c = { .sink = sk, .frame = PCG_FRAME_IDENTITY };
        unsigned int skip = (1u << face) | PRIM_BOX_POS_A;

        CERR_RET(pcg_band(&c, .lo = { 1.0f, 0.0f, 1.0f }, .hi = { 0.0f, 1.0f, 0.0f }, .surf = s,
                          .skip_mask = skip),
                 return EXIT_FAILURE);
        if (pcg_sink_get_stats(sk).nr_vertices != (face ? 24u : 30u))   return EXIT_FAILURE;

        /* the skipped faces are the ones gone: no normal points their way */
        struct sink_walk sw = {};
        if (failed(pcg_sink_for_each_mesh(sk, sink_walk_fn, &sw)) || sw.nr != 1)    return EXIT_FAILURE;
        for (int k = 0; k < 6; k++)
            if (mesh_count_normals(sw.mesh[0], &c.frame, face_dir[k]) != (skip & (1u << k) ? 0u : 6u))
                return EXIT_FAILURE;
    }

    if (!failed(pcg_band(&ctx, .lo = { 0.0f, 0.0f, 0.0f }, .hi = { 1.0f, 0.0f, 1.0f }, .surf = s))) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int shape_pane_test(void)
{
    vec2 org = { 3.0f, -2.0f }, dir = { 0.8f, 0.6f };
    const pcg_surface s = { .mat = &concrete, .collide = PCG_COLLIDE_SOLID };
    /* full height, a door with a transom, a gap without one */
    const struct { float y0, y1, h; } cases[] = { { 0.0f, 2.8f, 0.0f }, { 0.0f, 2.8f, 2.1f }, { 0.9f, 2.4f, 0.0f } };

    for (size_t i = 0; i < array_size(cases); i++)
        for (int gap = 0; gap < 2; gap++) {
            LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("pane"), return EXIT_FAILURE);
            pcg_ctx ctx = { .sink = sink, .frame = { .org = { org[0], 0.0f, org[1] }, .dir = { dir[0], dir[1] } } };
            const pcg_opening door = { .at = 2.0f, .w = 1.0f, .h = cases[i].h };

            CERR_RET(pcg_pane(&ctx, .open = &door, .nr_open = gap, .len = 4.0f, .y0 = cases[i].y0,
                              .y1 = cases[i].y1, .min_seg = 0.01f, .surf = s),
                     return EXIT_FAILURE);

            struct sink_walk w = {};
            if (failed(pcg_sink_for_each_mesh(sink, sink_walk_fn, &w)) || w.nr != 1)    return EXIT_FAILURE;

            /* the original pane, vertex for vertex */
            LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref"), return EXIT_FAILURE);
            if (failed(ref_glass_emit(ref, org, dir, 4.0f, cases[i].y0, cases[i].y1, 2.0f, gap ? 1.0f : 0.0f,
                                      cases[i].h, 1.0f)))
                return EXIT_FAILURE;
            if (!mesh_same_vx(w.mesh[0], ref, 0.0f, 0.0f))                  return EXIT_FAILURE;

            /* closed both ways, so enclosing nothing */
            if (!walk_closed(&w) || fabs(walk_volume(&w)) > 1e-4)   return EXIT_FAILURE;
            if (!walk_aabb_is(&w, &ctx.frame, (vec3){ 0.0f, cases[i].y0, 0.0f },
                              (vec3){ 4.0f, cases[i].y1, 0.0f }))
                return EXIT_FAILURE;
        }

    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("pane"), return EXIT_FAILURE);
    pcg_ctx ctx = { .sink = sink, .frame = PCG_FRAME_IDENTITY };

    /* a sliver next to an opening is left out */
    const pcg_opening edge = { .at = 0.5f + 0.005f, .w = 1.0f, .h = 2.1f };
    CERR_RET(pcg_pane(&ctx, .open = &edge, .nr_open = 1, .len = 4.0f, .y1 = 2.8f, .min_seg = 0.01f, .surf = s),
             return EXIT_FAILURE);
    if (pcg_sink_get_stats(sink).nr_vertices != 2 * 12) return EXIT_FAILURE;

    /* with no minimum, the empty runs at the start and between abutting openings are still left out */
    LOCAL_SET(pcg_sink, flush) = CRES_RET(pcg_sink_new("pane"), return EXIT_FAILURE);
    pcg_ctx fc = { .sink = flush, .frame = PCG_FRAME_IDENTITY };
    const pcg_opening abut[2] = { { .at = 0.5f, .w = 1.0f }, { .at = 1.5f, .w = 1.0f } };
    CERR_RET(pcg_pane(&fc, .open = abut, .nr_open = 2, .len = 4.0f, .y1 = 2.8f, .surf = s), return EXIT_FAILURE);

    struct sink_walk fw = {};
    if (failed(pcg_sink_for_each_mesh(flush, sink_walk_fn, &fw)) || fw.nr != 1)                 return EXIT_FAILURE;
    if (mesh_nr_vx(fw.mesh[0]) != 12 || !mesh_tangents_ok(fw.mesh[0]))                          return EXIT_FAILURE;
    if (!walk_aabb_is(&fw, &fc.frame, (vec3){ 2.0f, 0.0f, 0.0f }, (vec3){ 4.0f, 2.8f, 0.0f }))  return EXIT_FAILURE;

    /* openings that don't fit, no height */
    const pcg_opening past = { .at = 3.8f, .w = 1.0f };
    if (!failed(pcg_pane(&ctx, .open = &past, .nr_open = 1, .len = 4.0f, .y1 = 2.8f, .surf = s)))   return EXIT_FAILURE;
    if (!failed(pcg_pane(&ctx, .len = 4.0f, .y0 = 1.0f, .y1 = 1.0f, .surf = s)))                    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int shape_rod_test(void)
{
    /* a sloped square rail, flat side up, as the stair rails are */
    const vec3 a = { 1.0f, 0.5f, 2.0f }, b = { 6.0f, 3.5f, 4.0f };
    const float r = 0.07f;
    const pcg_surface s = { .mat = &concrete, .collide = PCG_COLLIDE_HAZARD };

    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("rod"), return EXIT_FAILURE);
    pcg_ctx ctx = { .sink = sink, .frame = PCG_FRAME_IDENTITY };

    CERR_RET(pcg_rod(&ctx, .a = { a[0], a[1], a[2] }, .b = { b[0], b[1], b[2] }, .ra = r, .rb = r,
                     .phase = (float)M_PI / 4.0f, .sides = 4, .surf = s),
             return EXIT_FAILURE);

    struct sink_walk w = {};
    if (failed(pcg_sink_for_each_mesh(sink, sink_walk_fn, &w)) || w.nr != 1)    return EXIT_FAILURE;

    /* the core prism's vertices, now with meter UVs and tangents */
    LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref"), return EXIT_FAILURE);
    if (failed(prim_emit_prism((float *)a, (float *)b, r, r, 4, (float)M_PI / 4.0f, .mesh = ref)))  return EXIT_FAILURE;
    if (mesh_nr_vx(ref) != mesh_nr_vx(w.mesh[0]))                                                   return EXIT_FAILURE;
    for (size_t v = 0; v < mesh_nr_vx(ref); v++)
        if (!vec3_near(&mesh_vx(ref)[v * 3], &mesh_vx(w.mesh[0])[v * 3], 0.0f))                     return EXIT_FAILURE;
    if (!mesh_tangents_ok(w.mesh[0]))                                                               return EXIT_FAILURE;

    /* U runs the axis in meters, V around the perimeter */
    vec3 axis;
    vec3_sub(axis, b, a);
    float len = vec3_len(axis), side = 2.0f * r * sinf((float)M_PI / 4.0f);
    float umax = 0.0f, vmax = 0.0f;
    for (size_t v = 0; v < mesh_nr_vx(w.mesh[0]); v++) {
        umax = fmaxf(umax, mesh_tx(w.mesh[0])[v * 2]);
        vmax = fmaxf(vmax, mesh_tx(w.mesh[0])[v * 2 + 1]);
    }
    if (!near(umax, len, 1e-5f) || !near(vmax, 4.0f * side, 1e-5f)) return EXIT_FAILURE;

    if (!walk_closed(&w))                                                   return EXIT_FAILURE;
    double vol = 2.0 * r * r * len;
    if (fabs(walk_volume(&w) - vol) > 1e-3 * vol)                           return EXIT_FAILURE;

    /* a tapered vertical rod: the cross-section turns to dodge the vertical axis */
    LOCAL_SET(pcg_sink, sk) = CRES_RET(pcg_sink_new("rod"), return EXIT_FAILURE);
    pcg_ctx c = { .sink = sk, .frame = PCG_FRAME_IDENTITY };
    CERR_RET(pcg_rod(&c, .a = { 0.0f, 0.0f, 0.0f }, .b = { 0.0f, 2.0f, 0.0f }, .ra = 0.3f, .rb = 0.1f,
                     .sides = 8, .surf = s),
             return EXIT_FAILURE);
    w = (struct sink_walk){};
    if (failed(pcg_sink_for_each_mesh(sk, sink_walk_fn, &w)) || !walk_closed(&w))   return EXIT_FAILURE;
    double a0 = ngon_area(8, 0.3), a1 = ngon_area(8, 0.1);
    vol = 2.0 / 3.0 * (a0 + a1 + sqrt(a0 * a1));
    if (fabs(walk_volume(&w) - vol) > 1e-3 * vol)                           return EXIT_FAILURE;

    /* bad rods */
    if (!failed(pcg_rod(&c, .b = { 1.0f, 0.0f, 0.0f }, .ra = 0.1f, .rb = 0.1f, .sides = 2, .surf = s)))
        return EXIT_FAILURE;
    if (!failed(pcg_rod(&c, .b = { 1.0f, 0.0f, 0.0f }, .ra = 0.0f, .rb = 0.1f, .sides = 4, .surf = s)))
        return EXIT_FAILURE;
    if (!failed(pcg_rod(&c, .b = { 0.0f, 0.0f, 0.0f }, .ra = 0.1f, .rb = 0.1f, .sides = 4, .surf = s)))
        return EXIT_FAILURE;

    /* more sides than a mesh holds, also where sides * 12 wraps a 32-bit size_t */
    const int many[] = { PCG_ROD_SIDES_MAX + 1, 357913942, INT_MAX };
    for (size_t k = 0; k < array_size(many); k++) {
        cerr err = pcg_rod(&c, .b = { 1.0f, 0.0f, 0.0f }, .ra = 0.1f, .rb = 0.1f, .sides = many[k], .surf = s);

        if (!IS_CERR_CODE(err, CERR_TOO_LARGE))                             return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/* emit one of each shape at @frame */
static cerr shape_all(pcg_sink *sink, const pcg_frame *frame)
{
    pcg_ctx ctx = { .sink = sink, .frame = *frame };
    const pcg_surface s = { .mat = &concrete, .collide = PCG_COLLIDE_SOLID };
    const poly_ring rings[2] = { { 0, 6 }, { 6, 6 } };
    vec2 pts[12];

    ring_plate_pts(pts, 6, 3.0f, 1.0f);
    CERR_RET_CERR(pcg_plate(&ctx, .pts = pts, .nr_pts = 12, .rings = rings, .nr_rings = 2, .y0 = -0.3f,
                            .y1 = 0.0f, .top = s, .bottom = s, .edge = s));
    CERR_RET_CERR(pcg_band(&ctx, .lo = { 4.0f, -1.0f, -0.1f }, .hi = { 7.0f, 0.0f, 0.2f }, .surf = s));
    CERR_RET_CERR(pcg_rod(&ctx, .a = { 0.0f, 1.0f, 5.0f }, .b = { 4.0f, 3.0f, 6.0f }, .ra = 0.1f, .rb = 0.1f,
                          .phase = (float)M_PI / 4.0f, .sides = 4, .surf = s));
    /* a square leg: vertical, so its cross-section can't hang off the up axis */
    CERR_RET_CERR(pcg_rod(&ctx, .a = { -6.0f, -1.0f, 2.0f }, .b = { -6.0f, 1.5f, 2.0f }, .ra = 0.05f, .rb = 0.05f,
                          .phase = (float)M_PI / 4.0f, .sides = 4, .surf = s));

    /* a pane on its own: it encloses nothing */
    pcg_frame side = { .org = { 0.0f, 0.0f, -6.0f }, .dir = { 0.6f, 0.8f } };
    pcg_ctx pane = pcg_ctx_at(&ctx, &side);
    const pcg_opening door = { .at = 2.0f, .w = 1.0f, .h = 2.1f };

    return pcg_pane(&pane, .open = &door, .nr_open = 1, .len = 4.0f, .y1 = 2.8f, .min_seg = 0.01f, .surf = s);
}

static int shape_frame_test(void)
{
    LOCAL_SET(pcg_sink, ref) = CRES_RET(pcg_sink_new("frame"), return EXIT_FAILURE);
    if (failed(shape_all(ref, &PCG_FRAME_IDENTITY)))    return EXIT_FAILURE;

    struct sink_walk rw = {};
    if (failed(pcg_sink_for_each_mesh(ref, sink_walk_fn, &rw)) || rw.nr != 1)   return EXIT_FAILURE;
    if (!walk_closed(&rw))                                                      return EXIT_FAILURE;
    double vol = walk_volume(&rw);
    vec3 lo, hi;
    walk_aabb(&rw, &PCG_FRAME_IDENTITY, lo, hi);

    for (int i = 0; i < 6; i++) {
        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("frame"), return EXIT_FAILURE);
        pcg_frame f;

        test_frame(&f, 0.9f * i - 1.3f, i & 1, 4.0f * i, 0.5f * i, -3.0f * i);
        if (failed(shape_all(sink, &f)))    return EXIT_FAILURE;

        struct sink_walk w = {};
        if (failed(pcg_sink_for_each_mesh(sink, sink_walk_fn, &w)) || w.nr != 1)    return EXIT_FAILURE;
        struct mesh *m = w.mesh[0], *rm = rw.mesh[0];
        if (mesh_nr_vx(m) != mesh_nr_vx(rm))        return EXIT_FAILURE;

        /* yawed or mirrored, still closed and facing out */
        if (!walk_closed(&w))                                   return EXIT_FAILURE;
        if (fabs(walk_volume(&w) - vol) > 1e-3 * fabs(vol))     return EXIT_FAILURE;
        if (!walk_aabb_is(&w, &f, lo, hi))                      return EXIT_FAILURE;
        if (!mesh_tangents_ok(m))                               return EXIT_FAILURE;

        /* unmirrored, every vertex comes back to where the identity put it */
        if (f.mirror)   continue;
        for (size_t v = 0; v < mesh_nr_vx(m); v++) {
            vec3 p, n;

            pcg_frame_point_to_local(&f, &mesh_vx(m)[v * 3], p);
            pcg_frame_vector_to_local(&f, &mesh_norm(m)[v * 3], n);
            if (!vec3_near(p, &mesh_vx(rm)[v * 3], 1e-4f))      return EXIT_FAILURE;
            if (!vec3_near(n, &mesh_norm(rm)[v * 3], 1e-4f))    return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Elements
 ****************************************************************************/

/* @sink's meshes; false if the walk fails or finds more than SINK_MESHES_MAX */
static bool sink_walk_get(pcg_sink *sink, struct sink_walk *w)
{
    *w = (struct sink_walk){};

    return !failed(pcg_sink_for_each_mesh(sink, sink_walk_fn, w));
}

/* the first mesh of @mat's bucket in @w, nullptr if it has none */
static struct mesh *walk_mesh(const struct sink_walk *w, const pcg_material *mat)
{
    for (size_t i = 0; i < w->nr; i++)
        if (w->key[i].mat == mat)   return w->mesh[i];

    return nullptr;
}

/* the triangle keys of all of @m together, sorted; @nr gets their number */
static tri_key *meshes_tri_keys(struct mesh *const *m, size_t nr_m, size_t *nr)
{
    *nr = 0;
    for (size_t i = 0; i < nr_m; i++)   *nr += mesh_nr_vx(m[i]) / 3;

    tri_key *keys = mem_alloc(sizeof(*keys), .nr = *nr ? *nr : 1);
    size_t at = 0;

    for (size_t i = 0; i < nr_m; i++) {
        size_t n = mesh_nr_vx(m[i]) / 3;
        LOCAL_SET(void, part) = mesh_tri_keys(m[i]);

        memcpy(&keys[at], part, n * sizeof(*keys));
        at += n;
    }
    qsort(keys, *nr, sizeof(*keys), tri_key_cmp);

    return keys;
}

/* the triangles of all of @a are those of all of @b, in any order */
static bool meshes_same_tris(struct mesh *const *a, size_t nr_a, struct mesh *const *b, size_t nr_b)
{
    size_t na, nb;
    LOCAL_SET(void, ka) = meshes_tri_keys(a, nr_a, &na);
    LOCAL_SET(void, kb) = meshes_tri_keys(b, nr_b, &nb);

    return na == nb && !memcmp(ka, kb, na * sizeof(tri_key));
}

/* every vertex of @m within [@lo, @hi] on @f's local y */
static bool mesh_y_within(struct mesh *m, const pcg_frame *f, float lo, float hi)
{
    for (size_t v = 0; v < mesh_nr_vx(m); v++) {
        float y = mesh_vx(m)[v * 3 + 1] - f->org[1];

        if (y < lo - 1e-5f || y > hi + 1e-5f)   return false;
    }

    return true;
}

/* the same bytes in every attribute */
static bool meshes_same_bytes(struct mesh *a, struct mesh *b)
{
    size_t nr = mesh_nr_vx(a);

    return nr == mesh_nr_vx(b) &&
           !memcmp(mesh_vx(a), mesh_vx(b), nr * sizeof(vec3)) &&
           !memcmp(mesh_tx(a), mesh_tx(b), nr * sizeof(vec2)) &&
           !memcmp(mesh_norm(a), mesh_norm(b), nr * sizeof(vec3)) &&
           !memcmp(mesh_tangent(a), mesh_tangent(b), nr * sizeof(vec4));
}

/*
 * Parity with the original emitters: the same triangles to PARITY_EPS in
 * position and effective texture coordinates (UV times the material's
 * density, which the originals baked into the vertices). Normals and
 * tangents are derived from those per triangle, across its shortest
 * edges: a last-bit difference in a position some 30 m out, over a face
 * a few centimeters wide, turns them by up to a few 1e-5. An element
 * that places its corners another way than its original gets
 * PARITY_DIR_EPS for them, and a triangle narrower than 2 * PARITY_EPS /
 * PARITY_DIR_EPS gets as much as two positions PARITY_EPS apart turn it
 * by across its shortest edge (see parity_dir_eps()); one that places
 * them the same way is held to PARITY_EPS (parity_check_exact()).
 */
#define PARITY_EPS      1e-5f
#define PARITY_DIR_EPS  1e-4f

typedef struct parity_vx {
    vec3    p;
    vec3    n;
    vec4    t;
    vec2    uv;
} parity_vx;

/* the largest differences of the matched vertices, per attribute */
typedef struct parity_err {
    float   p;
    float   n;
    float   t;
    float   uv;
} parity_err;

static float max_diff(const float *a, const float *b, int nr)
{
    float d = 0.0f;

    for (int i = 0; i < nr; i++)    d = fmaxf(d, fabsf(a[i] - b[i]));

    return d;
}

/* vertex @v of @m, its texture coordinates scaled by @density */
static void parity_vx_of(struct mesh *m, size_t v, float density, parity_vx *out)
{
    memcpy(out->p, &mesh_vx(m)[v * 3], sizeof(vec3));
    memcpy(out->n, &mesh_norm(m)[v * 3], sizeof(vec3));
    memcpy(out->t, &mesh_tangent(m)[v * 4], sizeof(vec4));
    out->uv[0] = mesh_tx(m)[v * 2] * density;
    out->uv[1] = mesh_tx(m)[v * 2 + 1] * density;
}

/*
 * The triangles of @ref, emitted by an original emitter in its own space,
 * as that emitter would have emitted them placed by unmirrored @f: the
 * positions placed, the charts of run-local faces carried along, and,
 * under @planar, horizontal faces charting the placed (x, z) at the
 * original @density. Normals and tangents follow from those through the
 * same core code that computes the elements' own.
 */
static cresp(mesh) parity_expect(struct mesh *ref, const pcg_frame *f, bool planar, float density)
{
    size_t nr = mesh_nr_vx(ref);
    LOCAL_SET(mesh_t, m) = CRES_RET_T(ref_new_checked(mesh, .name = "expect"), mesh);

    CRES_RET_T(_prim_begin(nr, &(prim_emit_opts){ .mesh = m, .tangents = true }), mesh);
    for (size_t v = 0; v + 2 < nr; v += 3) {
        vec3 p[3];
        vec2 uv[3];

        for (int k = 0; k < 3; k++) {
            parity_vx e;

            parity_vx_of(ref, v + k, 1.0f, &e);
            pcg_frame_point(f, e.p, p[k]);
            bool cap = planar && fabsf(e.n[1]) > 0.5f;
            uv[k][0] = cap ? p[k][0] * density : e.uv[0];
            uv[k][1] = cap ? p[k][2] * density : e.uv[1];
        }
        _prim_emit_triangle3(p[0], p[1], p[2], &(prim_emit_opts){ .mesh = m, .uv = uv });
    }

    return cresp_val(mesh, NOCU(m));
}

/*
 * The normal and tangent tolerance of the expected triangle @tri: two
 * corners PARITY_EPS off either way turn it by up to 2 * PARITY_EPS over
 * its shortest edge, and never less than PARITY_DIR_EPS
 */
static float parity_dir_eps(const parity_vx *tri)
{
    float edge = INFINITY;

    for (int k = 0; k < 3; k++) {
        vec3 d;

        vec3_sub(d, tri[(k + 1) % 3].p, tri[k].p);
        edge = fminf(edge, vec3_len(d));
    }

    return fmaxf(PARITY_DIR_EPS, 2.0f * PARITY_EPS / edge);
}

/* without @charts, texture coordinates and the tangents that follow them don't count */
static bool parity_vx_near(const parity_vx *a, const parity_vx *b, bool charts, float dir_eps, parity_err *e)
{
    parity_err d = {
        .p  = max_diff(a->p, b->p, 3),
        .n  = max_diff(a->n, b->n, 3),
        .t  = charts ? max_diff(a->t, b->t, 4) : 0.0f,
        .uv = charts ? max_diff(a->uv, b->uv, 2) : 0.0f,
    };

    if (d.p > PARITY_EPS || d.uv > PARITY_EPS || d.n > dir_eps || d.t > dir_eps)    return false;

    e->p = fmaxf(e->p, d.p);
    e->n = fmaxf(e->n, d.n);
    e->t = fmaxf(e->t, d.t);
    e->uv = fmaxf(e->uv, d.uv);

    return true;
}

/*
 * @m, emitted by an element at @f with meter UVs, holds the triangles of
 * @ref, emitted by the original at its own origin with @density repeats
 * per meter and placed by @f (see parity_expect()). Triangles may come in
 * any order and start at any corner, but wind the same way. Without
 * @charts, only positions and normals are compared. Under @exact, normals
 * and tangents get PARITY_EPS, otherwise parity_dir_eps().
 */
static bool parity_match(const char *what, struct mesh *m, struct mesh *ref, const pcg_frame *f, bool planar,
                         float density, bool charts, bool exact)
{
    if (!m || !ref) {
        fprintf(stderr, "%s: parity: missing mesh\n", what);
        return false;
    }

    size_t nr = mesh_nr_vx(m), nr_tris = nr / 3;
    if (nr != mesh_nr_vx(ref) || nr % 3) {
        fprintf(stderr, "%s: parity: %zu vertices, the original has %zu\n", what, nr, mesh_nr_vx(ref));
        return false;
    }

    LOCAL_SET(mesh_t, em) = CRES_RET(parity_expect(ref, f, planar, density), return false);
    LOCAL_SET(void, ebuf) = mem_alloc(sizeof(parity_vx), .nr = nr ? nr : 1);
    LOCAL_SET(void, ubuf) = mem_alloc(sizeof(bool), .nr = nr_tris ? nr_tris : 1, .zero = 1);
    parity_vx *exp = ebuf;
    bool *used = ubuf;
    parity_err err = {};

    for (size_t v = 0; v < nr; v++) parity_vx_of(em, v, 1.0f, &exp[v]);

    for (size_t t = 0; t < nr_tris; t++) {
        parity_vx got[3];
        bool found = false;

        for (int k = 0; k < 3; k++) parity_vx_of(m, t * 3 + k, density, &got[k]);

        /* the same order first: then it's one look per triangle */
        for (size_t i = 0; i < nr_tris && !found; i++) {
            size_t s = (t + i) % nr_tris;

            if (used[s])    continue;

            float dir_eps = exact ? PARITY_EPS : parity_dir_eps(&exp[s * 3]);
            for (int rot = 0; rot < 3 && !found; rot++) {
                parity_err e = err;

                found = parity_vx_near(&got[0], &exp[s * 3 + rot], charts, dir_eps, &e) &&
                        parity_vx_near(&got[1], &exp[s * 3 + (rot + 1) % 3], charts, dir_eps, &e) &&
                        parity_vx_near(&got[2], &exp[s * 3 + (rot + 2) % 3], charts, dir_eps, &e);
                if (found) {
                    used[s] = true;
                    err = e;
                }
            }
        }

        if (!found) {
            fprintf(stderr, "%s: parity: triangle %zu at (%f, %f, %f) has no match\n", what, t,
                    got[0].p[0], got[0].p[1], got[0].p[2]);
            return false;
        }
    }

    if (verbose)
        printf("%s: parity: %zu triangles, max error p %g n %g t %g uv %g\n", what, nr_tris, err.p, err.n,
               err.t, err.uv);

    return true;
}

/* parity_match() of everything: the same triangles with the same charts */
static bool parity_check(const char *what, struct mesh *m, struct mesh *ref, const pcg_frame *f, bool planar,
                         float density)
{
    return parity_match(what, m, ref, f, planar, density, true, false);
}

/* parity_check() with normals and tangents to PARITY_EPS as well */
static bool parity_check_exact(const char *what, struct mesh *m, struct mesh *ref, const pcg_frame *f,
                               bool planar, float density)
{
    return parity_match(what, m, ref, f, planar, density, true, true);
}

/* parity_match() of the shape alone, for an accepted change of charts */
static bool parity_check_shape(const char *what, struct mesh *m, struct mesh *ref, const pcg_frame *f)
{
    return parity_match(what, m, ref, f, false, 1.0f, false, false);
}

/* emit an element into @ctx, with its params and site in @priv */
typedef cerr (*element_fn)(const pcg_ctx *ctx, const void *priv);

/* a fresh sink with what @fn emits at @f */
static cresp(pcg_sink) element_sink(element_fn fn, const void *priv, const pcg_palette *pal, const pcg_frame *f)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET_T(pcg_sink_new("element"), pcg_sink);
    pcg_ctx ctx = { .sink = sink, .palette = pal, .frame = *f };

    CERR_RET_T(fn(&ctx, priv), pcg_sink);

    return cresp_val(pcg_sink, NOCU(sink));
}

/*
 * What every element must do under any frame: emit the same buckets with
 * the same vertex counts, over the same local bounding box, with unit
 * tangents and, for a @solid, as a closed surface of the same positive
 * volume, so mirrored faces still point out; unmirrored, every vertex
 * lands back where the identity frame put it; and emitting twice gives
 * the same bytes.
 */
static bool element_frames_ok(element_fn fn, const void *priv, const pcg_palette *pal, bool solid)
{
    LOCAL_SET(pcg_sink, ref) = CRES_RET(element_sink(fn, priv, pal, &PCG_FRAME_IDENTITY), return false);
    struct sink_walk rw;

    if (!sink_walk_get(ref, &rw) || !rw.nr)                 return false;

    double vol = walk_volume(&rw);
    if (solid && (!walk_closed(&rw) || !(vol > 0.0)))       return false;

    vec3 lo, hi;
    walk_aabb(&rw, &PCG_FRAME_IDENTITY, lo, hi);

    for (int i = 0; i < 6; i++) {
        pcg_frame f;

        test_frame(&f, 0.9f * i - 1.3f, i & 1, 4.0f * i, 0.5f * i, -3.0f * i);
        LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(fn, priv, pal, &f), return false);

        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != rw.nr)  return false;
        for (size_t b = 0; b < w.nr; b++) {
            if (w.key[b].mat != rw.key[b].mat)                      return false;
            if (mesh_nr_vx(w.mesh[b]) != mesh_nr_vx(rw.mesh[b]))    return false;
            if (!mesh_tangents_ok(w.mesh[b]))                       return false;
        }
        if (solid && !walk_closed(&w))                          return false;
        if (solid && fabs(walk_volume(&w) - vol) > 1e-4 * vol)  return false;
        if (!walk_aabb_is(&w, &f, lo, hi))                      return false;

        if (f.mirror)   continue;
        for (size_t b = 0; b < w.nr; b++)
            for (size_t v = 0; v < mesh_nr_vx(w.mesh[b]); v++) {
                vec3 p, n;

                pcg_frame_point_to_local(&f, &mesh_vx(w.mesh[b])[v * 3], p);
                pcg_frame_vector_to_local(&f, &mesh_norm(w.mesh[b])[v * 3], n);
                if (!vec3_near(p, &mesh_vx(rw.mesh[b])[v * 3], 1e-4f))      return false;
                if (!vec3_near(n, &mesh_norm(rw.mesh[b])[v * 3], 1e-4f))    return false;
            }
    }

    pcg_frame f;
    test_frame(&f, 2.1f, true, -7.0f, 3.0f, 11.0f);
    LOCAL_SET(pcg_sink, s0) = CRES_RET(element_sink(fn, priv, pal, &f), return false);
    LOCAL_SET(pcg_sink, s1) = CRES_RET(element_sink(fn, priv, pal, &f), return false);
    struct sink_walk w0, w1;

    if (!sink_walk_get(s0, &w0) || !sink_walk_get(s1, &w1) || w0.nr != w1.nr)   return false;
    for (size_t b = 0; b < w0.nr; b++)
        if (!meshes_same_bytes(w0.mesh[b], w1.mesh[b]))                         return false;

    return true;
}

/* fake finishes, one per surface, so each surface lands in its own bucket */
static const pcg_material surf_mat[] = {
    { .name = "s0", .uv_mode = PCG_UV_METERS }, { .name = "s1", .uv_mode = PCG_UV_METERS },
    { .name = "s2", .uv_mode = PCG_UV_METERS }, { .name = "s3", .uv_mode = PCG_UV_METERS },
    { .name = "s4", .uv_mode = PCG_UV_METERS }, { .name = "s5", .uv_mode = PCG_UV_METERS },
};

/* the reference layout's palette: concrete structure, wood floors, panel cladding */
static const pcg_material panel = { .name = "panel", .uv_mode = PCG_UV_METERS };
static const pcg_palette ref_palette = {
    .mat[PCG_ROLE_STRUCTURE]    = &concrete,
    .mat[PCG_ROLE_FLOOR]        = &wood,
    .mat[PCG_ROLE_CLADDING]     = &panel,
};

/* reference copies of the original texture densities, repeats per meter */
#define REF_UV_CONCRETE (1.0f / 2.4f)
#define REF_UV_FLOOR    (1.0f / 2.0f)
#define REF_UV_PANEL    (1.0f / 2.6f)

/****************************************************************************
 * Box
 ****************************************************************************/

/* outward normals of the box faces, by the bit number of their flags */
static const vec3 box_face_dir[6] = {
    [0] = { 1.0f, 0.0f, 0.0f }, [1] = { -1.0f, 0.0f, 0.0f }, [2] = { 0.0f, 1.0f, 0.0f },
    [3] = { 0.0f, -1.0f, 0.0f }, [4] = { 0.0f, 0.0f, 1.0f }, [5] = { 0.0f, 0.0f, -1.0f },
};

static const vec3 box_lo = { 1.0f, 0.5f, -0.3f }, box_hi = { 2.5f, 3.0f, 0.4f };

static cerr box_emit(const pcg_ctx *ctx, const void *priv)
{
    const pcg_surface *s = priv;

    return pcg_box(ctx, .lo = { box_lo[0], box_lo[1], box_lo[2] }, .hi = { box_hi[0], box_hi[1], box_hi[2] },
                   .face = PCG_BOX_ALL(s));
}

static int shape_box_test(void)
{
    const pcg_surface s = { .mat = &concrete, .collide = PCG_COLLIDE_SOLID };

    /* the core box's vertices and charts, at one repeat per meter */
    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(box_emit, &s, nullptr, &PCG_FRAME_IDENTITY),
                                         return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(sink, &w) || w.nr != 1)                          return EXIT_FAILURE;

    LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref"), return EXIT_FAILURE);
    const prim_box core = {
        .dir = { 1.0f, 0.0f }, .uv = 1.0f,
        .lo = { box_lo[0], box_lo[1], box_lo[2] }, .hi = { box_hi[0], box_hi[1], box_hi[2] },
    };
    if (failed(prim_emit_box(&core, .mesh = ref, .tangents = true)))    return EXIT_FAILURE;
    if (!mesh_same_vx(w.mesh[0], ref, 0.0f, 0.0f))                      return EXIT_FAILURE;
    if (!mesh_tangents_ok(w.mesh[0]))                                   return EXIT_FAILURE;

    /* reversed ranges come out the same */
    LOCAL_SET(pcg_sink, rs) = CRES_RET(pcg_sink_new("box"), return EXIT_FAILURE);
    pcg_ctx rc = { .sink = rs, .frame = PCG_FRAME_IDENTITY };
    CERR_RET(pcg_box(&rc, .lo = { box_hi[0], box_lo[1], box_hi[2] }, .hi = { box_lo[0], box_hi[1], box_lo[2] },
                     .face = PCG_BOX_ALL(&s)),
             return EXIT_FAILURE);
    struct sink_walk rw;
    if (!sink_walk_get(rs, &rw) || !mesh_same_vx(rw.mesh[0], ref, 0.0f, 0.0f))  return EXIT_FAILURE;

    /* raised and moved, the box keeps its charts: they are local, heights included */
    pcg_frame up = { .org = { 3.0f, 12.0f, -4.0f }, .dir = { 1.0f, 0.0f } };
    LOCAL_SET(pcg_sink, us) = CRES_RET(element_sink(box_emit, &s, nullptr, &up), return EXIT_FAILURE);
    struct sink_walk uw;
    if (!sink_walk_get(us, &uw) || mesh_nr_vx(uw.mesh[0]) != 36)    return EXIT_FAILURE;
    for (size_t v = 0; v < 36; v++) {
        const float *p = &mesh_vx(uw.mesh[0])[v * 3], *q = &mesh_vx(ref)[v * 3];

        if (!vec3_near(p, (vec3){ q[0] + 3.0f, q[1] + 12.0f, q[2] - 4.0f }, 1e-5f))     return EXIT_FAILURE;
        if (memcmp(&mesh_tx(uw.mesh[0])[v * 2], &mesh_tx(ref)[v * 2], sizeof(vec2)))    return EXIT_FAILURE;
    }

    /* closed and outward under any frame */
    if (!element_frames_ok(box_emit, &s, nullptr, true))                return EXIT_FAILURE;

    /* swapped, every corner of every face has its U and V traded */
    pcg_surface sw = s;
    sw.uv_swap = true;
    LOCAL_SET(pcg_sink, ss) = CRES_RET(element_sink(box_emit, &sw, nullptr, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    struct sink_walk swk;
    if (!sink_walk_get(ss, &swk) || mesh_nr_vx(swk.mesh[0]) != 36)      return EXIT_FAILURE;
    if (!mesh_tangents_ok(swk.mesh[0]))                                 return EXIT_FAILURE;
    for (size_t v = 0; v < 36; v++) {
        const float *p = &mesh_vx(swk.mesh[0])[v * 3], *n = &mesh_norm(swk.mesh[0])[v * 3];
        const float *t = &mesh_tx(swk.mesh[0])[v * 2];
        bool found = false;

        for (size_t u = 0; u < 36 && !found; u++)
            found = vec3_near(p, &mesh_vx(ref)[u * 3], 0.0f) && vec3_near(n, &mesh_norm(ref)[u * 3], 1e-6f) &&
                    t[0] == mesh_tx(ref)[u * 2 + 1] && t[1] == mesh_tx(ref)[u * 2];
        if (!found)     return EXIT_FAILURE;
    }

    /* an empty range */
    if (!failed(pcg_box(&rc, .lo = { 0.0f, 0.0f, 0.0f }, .hi = { 1.0f, 0.0f, 1.0f }, .face = PCG_BOX_ALL(&s))))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const vec4 box_rect = { 0.05f, 0.1f, 0.4f, 0.35f };

static cerr box_rect_emit(const pcg_ctx *ctx, const void *priv)
{
    const pcg_surface *s = priv;

    return pcg_box(ctx, .lo = { box_lo[0], box_lo[1], box_lo[2] }, .hi = { box_hi[0], box_hi[1], box_hi[2] },
                   .face = PCG_BOX_ALL(s), .uv_rect = box_rect);
}

static int shape_box_rect_test(void)
{
    const pcg_surface s = { .mat = &decal };

    /* the core box over one rectangle, corner for corner */
    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(box_rect_emit, &s, nullptr, &PCG_FRAME_IDENTITY),
                                         return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(sink, &w) || w.nr != 1)                          return EXIT_FAILURE;

    LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref"), return EXIT_FAILURE);
    const prim_box core = {
        .dir = { 1.0f, 0.0f }, .uv_rect = box_rect,
        .lo = { box_lo[0], box_lo[1], box_lo[2] }, .hi = { box_hi[0], box_hi[1], box_hi[2] },
    };
    if (failed(prim_emit_box(&core, .mesh = ref, .tangents = true)))    return EXIT_FAILURE;
    if (!mesh_same_vx(w.mesh[0], ref, 0.0f, 0.0f))                      return EXIT_FAILURE;
    if (!mesh_tangents_ok(w.mesh[0]))                                   return EXIT_FAILURE;

    /* placed by a frame, the same box turned with it, on the same rectangle */
    pcg_frame f;
    test_frame(&f, 2.2f, false, -4.0f, 1.5f, 6.0f);
    LOCAL_SET(pcg_sink, ys) = CRES_RET(element_sink(box_rect_emit, &s, nullptr, &f), return EXIT_FAILURE);
    if (!sink_walk_get(ys, &w) || w.nr != 1)                            return EXIT_FAILURE;
    if (!parity_check("box rect", w.mesh[0], ref, &f, false, 1.0f))     return EXIT_FAILURE;

    /* closed and outward under any frame */
    if (!element_frames_ok(box_rect_emit, &s, nullptr, true))           return EXIT_FAILURE;

    /* a meter material doesn't take a rectangle; a rectangle that isn't finite is an error */
    LOCAL_SET(pcg_sink, bs) = CRES_RET(pcg_sink_new("box"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bs, .frame = PCG_FRAME_IDENTITY };
    const pcg_surface m = { .mat = &concrete };
    if (!failed(box_rect_emit(&bc, &m)))                                return EXIT_FAILURE;
    for (int k = 0; k < 4; k++) {
        vec4 bad = { 0.0f, 0.0f, 1.0f, 1.0f };

        bad[k] = NAN;
        if (!failed_inval(pcg_box(&bc, .lo = { 0.0f, 0.0f, 0.0f }, .hi = { 1.0f, 1.0f, 1.0f },
                                  .face = PCG_BOX_ALL(&s), .uv_rect = bad)))
            return EXIT_FAILURE;
    }
    if (!sink_walk_get(bs, &w) || w.nr)                                 return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int shape_box_faces_test(void)
{
    /* a surface per face: six buckets of one face each */
    pcg_surface surf[6];
    for (int k = 0; k < 6; k++) surf[k] = (pcg_surface){ .mat = &surf_mat[k], .collide = PCG_COLLIDE_SOLID };

    pcg_frame f;
    test_frame(&f, 0.7f, true, 2.0f, 3.0f, -1.0f);
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("box"), return EXIT_FAILURE);
    pcg_ctx ctx = { .sink = sink, .frame = f };
    CERR_RET(pcg_box(&ctx, .lo = { box_lo[0], box_lo[1], box_lo[2] }, .hi = { box_hi[0], box_hi[1], box_hi[2] },
                     .face = { &surf[0], &surf[1], &surf[2], &surf[3], &surf[4], &surf[5] }),
             return EXIT_FAILURE);

    struct sink_walk w;
    if (!sink_walk_get(sink, &w) || w.nr != 6 || !walk_closed(&w))  return EXIT_FAILURE;
    for (int k = 0; k < 6; k++) {
        struct mesh *m = walk_mesh(&w, &surf_mat[k]);

        if (!m || mesh_nr_vx(m) != 6 || !mesh_normals_along(m, &f, box_face_dir[k]))    return EXIT_FAILURE;
    }

    /* faces on one surface share its bucket, in flag order; missing and skipped faces are left out */
    const pcg_surface skipped = { .mat = &surf_mat[2], .skip = true };
    LOCAL_SET(pcg_sink, gs) = CRES_RET(pcg_sink_new("box"), return EXIT_FAILURE);
    pcg_ctx gc = { .sink = gs, .frame = PCG_FRAME_IDENTITY };
    CERR_RET(pcg_box(&gc, .lo = { box_lo[0], box_lo[1], box_lo[2] }, .hi = { box_hi[0], box_hi[1], box_hi[2] },
                     .face = { &surf[0], &surf[1], &surf[0], nullptr, &skipped, &surf[1] }),
             return EXIT_FAILURE);

    if (!sink_walk_get(gs, &w) || w.nr != 2)                            return EXIT_FAILURE;
    struct mesh *m0 = walk_mesh(&w, &surf_mat[0]), *m1 = walk_mesh(&w, &surf_mat[1]);
    if (!m0 || !m1 || mesh_nr_vx(m0) != 12 || mesh_nr_vx(m1) != 12) return EXIT_FAILURE;

    const pcg_frame *id = &PCG_FRAME_IDENTITY;
    if (mesh_count_normals(m0, id, box_face_dir[0]) != 6 || mesh_count_normals(m0, id, box_face_dir[2]) != 6)
        return EXIT_FAILURE;
    if (mesh_count_normals(m1, id, box_face_dir[1]) != 6 || mesh_count_normals(m1, id, box_face_dir[5]) != 6)
        return EXIT_FAILURE;
    if (!vec3_near(&mesh_norm(m0)[0], box_face_dir[0], 1e-6f))      return EXIT_FAILURE;
    if (!vec3_near(&mesh_norm(m0)[6 * 3], box_face_dir[2], 1e-6f))  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Cylinder
 ****************************************************************************/

/* a ceiling light's disc, off the origin */
static const vec3 cyl_org = { 1.0f, 0.5f, -2.0f };
#define CYL_H       0.05f
#define CYL_R       0.16f
#define CYL_SIDES   12

static cerr cylinder_emit(const pcg_ctx *ctx, const void *priv)
{
    const pcg_cylinder_desc *d = priv;

    return _pcg_cylinder(ctx, d);
}

/* the volume of a cylinder over a regular @n-gon of circumradius @r */
static double cylinder_volume(int n, double r, double h)
{
    return ngon_area(n, r) * h;
}

static int shape_cylinder_test(void)
{
    const pcg_cylinder_desc unit = {
        .org = { cyl_org[0], cyl_org[1], cyl_org[2] }, .h = CYL_H, .r = CYL_R, .sides = CYL_SIDES,
        .uv = PCG_UV_UNIT, .surf = { .mat = &decal },
    };
    pcg_cylinder_desc meters = unit;
    meters.uv = PCG_UV_METERS;
    meters.surf.mat = &concrete;

    /* the core cylinder's triangles and charts, bit for bit, at the identity */
    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(cylinder_emit, &unit, nullptr, &PCG_FRAME_IDENTITY),
                                         return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(sink, &w) || w.nr != 1)                              return EXIT_FAILURE;

    LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref"), return EXIT_FAILURE);
    CRES_RET(_prim_begin(CYL_SIDES * 12, &(prim_emit_opts){ .mesh = ref, .tangents = true }), return EXIT_FAILURE);
    if (failed(_prim_emit_cylinder((float *)cyl_org, CYL_H, CYL_R, CYL_SIDES, &(prim_emit_opts){ .mesh = ref })))
        return EXIT_FAILURE;
    if (!mesh_same_vx(w.mesh[0], ref, 0.0f, 0.0f))  return EXIT_FAILURE;

    /* and placed by a frame, the same cylinder turned with it */
    pcg_frame f;
    test_frame(&f, 0.6f, false, 5.0f, 12.0f, -3.0f);
    LOCAL_SET(pcg_sink, ys) = CRES_RET(element_sink(cylinder_emit, &unit, nullptr, &f), return EXIT_FAILURE);
    if (!sink_walk_get(ys, &w) || w.nr != 1)                                return EXIT_FAILURE;
    if (!parity_check("cylinder", w.mesh[0], ref, &f, false, 1.0f))         return EXIT_FAILURE;

    /* closed, over its n-gon, unit squares on every face */
    if (!sink_walk_get(sink, &w) || !walk_closed(&w))                       return EXIT_FAILURE;
    double vol = cylinder_volume(CYL_SIDES, CYL_R, CYL_H);
    if (fabs(walk_volume(&w) - vol) > 1e-4 * vol)                           return EXIT_FAILURE;
    const vec3 lo = { cyl_org[0] - CYL_R, cyl_org[1], cyl_org[2] - CYL_R };
    const vec3 hi = { cyl_org[0] + CYL_R, cyl_org[1] + CYL_H, cyl_org[2] + CYL_R };
    if (!walk_aabb_is(&w, &PCG_FRAME_IDENTITY, lo, hi))                     return EXIT_FAILURE;
    for (size_t v = 0; v < mesh_nr_vx(w.mesh[0]) * 2; v++)
        if (mesh_tx(w.mesh[0])[v] < 0.0f || mesh_tx(w.mesh[0])[v] > 1.0f)   return EXIT_FAILURE;
    if (mesh_count_normals(w.mesh[0], &PCG_FRAME_IDENTITY, box_face_dir[2]) != CYL_SIDES * 3 ||
        mesh_count_normals(w.mesh[0], &PCG_FRAME_IDENTITY, box_face_dir[3]) != CYL_SIDES * 3)
        return EXIT_FAILURE;

    /* in meters: U around the perimeter, V up */
    LOCAL_SET(pcg_sink, ms) = CRES_RET(element_sink(cylinder_emit, &meters, nullptr, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(ms, &w) || w.nr != 1 || !walk_closed(&w))            return EXIT_FAILURE;
    float umax = 0.0f, vmax = -INFINITY, side = 2.0f * CYL_R * sinf((float)M_PI / CYL_SIDES);
    for (size_t v = 0; v < mesh_nr_vx(w.mesh[0]); v++) {
        if (!near(mesh_norm(w.mesh[0])[v * 3 + 1], 0.0f, 1e-4f))    continue;
        umax = fmaxf(umax, mesh_tx(w.mesh[0])[v * 2]);
        vmax = fmaxf(vmax, mesh_tx(w.mesh[0])[v * 2 + 1]);
    }
    if (!near(umax, CYL_SIDES * side, 1e-5f) || !near(vmax, cyl_org[1] + CYL_H, 1e-6f)) return EXIT_FAILURE;

    /* both charts under any frame */
    if (!element_frames_ok(cylinder_emit, &unit, nullptr, true))    return EXIT_FAILURE;
    if (!element_frames_ok(cylinder_emit, &meters, nullptr, true))  return EXIT_FAILURE;

    /* skipped: nothing; a chart its material doesn't take, or a bad cylinder: an error */
    LOCAL_SET(pcg_sink, bs) = CRES_RET(pcg_sink_new("cylinder"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bs, .frame = PCG_FRAME_IDENTITY };
    pcg_cylinder_desc d = unit;
    d.surf.skip = true;
    CERR_RET(_pcg_cylinder(&bc, &d), return EXIT_FAILURE);
    d = unit;
    d.surf.mat = &concrete;
    if (!failed(_pcg_cylinder(&bc, &d)))                                    return EXIT_FAILURE;
    d = unit;
    d.sides = 2;
    if (!failed(_pcg_cylinder(&bc, &d)))                                    return EXIT_FAILURE;
    d = unit;
    d.r = 0.0f;
    if (!failed(_pcg_cylinder(&bc, &d)))                                    return EXIT_FAILURE;
    d = unit;
    d.h = -1.0f;
    if (!failed(_pcg_cylinder(&bc, &d)))                                    return EXIT_FAILURE;
    /* more sides than a mesh holds, also where sides * 12 wraps a 32-bit size_t */
    const int many[] = { PCG_CYLINDER_SIDES_MAX + 1, 357913942, INT_MAX };
    for (size_t k = 0; k < array_size(many); k++) {
        d = unit;
        d.sides = many[k];
        cerr err = _pcg_cylinder(&bc, &d);
        if (!IS_CERR_CODE(err, CERR_TOO_LARGE))                             return EXIT_FAILURE;
    }
    if (!sink_walk_get(bs, &w) || w.nr)                                     return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Quad
 ****************************************************************************/

/* a decal facing local +c, its rectangle running against U and V */
static const pcg_quad_desc quad_decal = {
    .c00 = { 0.5f, 0.2f, 0.3f }, .c10 = { 1.5f, 0.2f, 0.3f }, .c11 = { 1.5f, 1.0f, 0.3f }, .c01 = { 0.5f, 1.0f, 0.3f },
    .uv_rect = { 0.98f, 0.95f, 0.52f, 0.05f }, .uv = PCG_UV_UNIT, .surf = { .mat = &decal },
};

static cerr quad_emit(const pcg_ctx *ctx, const void *priv)
{
    return _pcg_quad(ctx, priv);
}

static int shape_quad_test(void)
{
    const pcg_quad_desc *q = &quad_decal;
    const float *r = q->uv_rect;

    /* two triangles, every corner on its own coordinates */
    LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref"), return EXIT_FAILURE);
    CRES_RET(_prim_begin(6, &(prim_emit_opts){ .mesh = ref, .tangents = true }), return EXIT_FAILURE);
    vec2 t0[3] = { { r[0], r[1] }, { r[2], r[1] }, { r[0], r[3] } };
    vec2 t1[3] = { { r[0], r[3] }, { r[2], r[1] }, { r[2], r[3] } };
    _prim_emit_triangle3((float *)q->c00, (float *)q->c10, (float *)q->c01, &(prim_emit_opts){ .mesh = ref, .uv = t0 });
    _prim_emit_triangle3((float *)q->c01, (float *)q->c10, (float *)q->c11, &(prim_emit_opts){ .mesh = ref, .uv = t1 });

    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(quad_emit, q, nullptr, &PCG_FRAME_IDENTITY),
                                         return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(sink, &w) || w.nr != 1)                                  return EXIT_FAILURE;
    if (!mesh_same_vx(w.mesh[0], ref, 0.0f, 0.0f))                              return EXIT_FAILURE;
    if (!mesh_normals_along(w.mesh[0], &PCG_FRAME_IDENTITY, box_face_dir[4]))   return EXIT_FAILURE;
    if (!mesh_tangents_ok(w.mesh[0]))                                           return EXIT_FAILURE;

    /* turned with a frame, and facing the same way in it mirrored or not */
    for (int m = 0; m < 2; m++) {
        pcg_frame f;

        test_frame(&f, 1.1f, m, 6.0f, 2.0f, -5.0f);
        LOCAL_SET(pcg_sink, fs) = CRES_RET(element_sink(quad_emit, q, nullptr, &f), return EXIT_FAILURE);
        if (!sink_walk_get(fs, &w) || w.nr != 1)                            return EXIT_FAILURE;
        if (!mesh_normals_along(w.mesh[0], &f, box_face_dir[4]))            return EXIT_FAILURE;
        if (!m && !parity_check("quad", w.mesh[0], ref, &f, false, 1.0f))   return EXIT_FAILURE;
    }
    if (!element_frames_ok(quad_emit, q, nullptr, false))   return EXIT_FAILURE;

    /* swapped, every corner has its U and V traded, still facing out */
    pcg_quad_desc d = *q;
    d.surf.uv_swap = true;
    LOCAL_SET(pcg_sink, ss) = CRES_RET(element_sink(quad_emit, &d, nullptr, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(ss, &w) || mesh_nr_vx(w.mesh[0]) != 6)                   return EXIT_FAILURE;
    if (!mesh_normals_along(w.mesh[0], &PCG_FRAME_IDENTITY, box_face_dir[4]))   return EXIT_FAILURE;
    for (size_t v = 0; v < 6; v++) {
        const float *p = &mesh_vx(w.mesh[0])[v * 3], *t = &mesh_tx(w.mesh[0])[v * 2];
        bool found = false;

        for (size_t u = 0; u < 6 && !found; u++)
            found = vec3_near(p, &mesh_vx(ref)[u * 3], 0.0f) &&
                    t[0] == mesh_tx(ref)[u * 2 + 1] && t[1] == mesh_tx(ref)[u * 2];
        if (!found)     return EXIT_FAILURE;
    }

    /* skipped: nothing; a mode its material doesn't take, or a bad quad: an error */
    LOCAL_SET(pcg_sink, bs) = CRES_RET(pcg_sink_new("quad"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bs, .frame = PCG_FRAME_IDENTITY };
    d = *q;
    d.surf.skip = true;
    CERR_RET(_pcg_quad(&bc, &d), return EXIT_FAILURE);
    d = *q;
    d.uv = PCG_UV_METERS;
    if (!failed(_pcg_quad(&bc, &d)))                                        return EXIT_FAILURE;
    d = *q;
    d.uv = PCG_UV_NR;
    if (!failed_inval(_pcg_quad(&bc, &d)))                                  return EXIT_FAILURE;
    d = *q;
    vec3_dup(d.c10, d.c00);
    if (!failed_inval(_pcg_quad(&bc, &d)))                                  return EXIT_FAILURE;
    d = *q;
    vec3_dup(d.c01, (vec3){ 2.5f, 0.2f, 0.3f });
    if (!failed_inval(_pcg_quad(&bc, &d)))                                  return EXIT_FAILURE;
    /* the second triangle of no area: @c11 on @c10 or @c01, or on the diagonal between them */
    d = *q;
    vec3_dup(d.c11, d.c10);
    if (!failed_inval(_pcg_quad(&bc, &d)))                                  return EXIT_FAILURE;
    d = *q;
    vec3_dup(d.c11, d.c01);
    if (!failed_inval(_pcg_quad(&bc, &d)))                                  return EXIT_FAILURE;
    d = *q;
    vec3_dup(d.c00, (vec3){ 0.0f, 0.0f, 0.3f });
    vec3_dup(d.c10, (vec3){ 1.0f, 0.0f, 0.3f });
    vec3_dup(d.c11, (vec3){ 0.5f, 0.5f, 0.3f });
    vec3_dup(d.c01, (vec3){ 0.0f, 1.0f, 0.3f });
    if (!failed_inval(_pcg_quad(&bc, &d)))                                  return EXIT_FAILURE;

    /* every corner coordinate and texture coordinate, infinite or NaN */
    for (int k = 0; k < 16; k++) {
        d = *q;
        float *field[] = { d.c00, d.c10, d.c11, d.c01 };
        float *x = k < 12 ? &field[k / 3][k % 3] : &d.uv_rect[k - 12];

        *x = (k & 1) ? INFINITY : NAN;
        if (!failed_inval(_pcg_quad(&bc, &d)))                              return EXIT_FAILURE;
    }
    if (!sink_walk_get(bs, &w) || w.nr)                                     return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Slab
 ****************************************************************************/

/* a slab of the reference layout: a hexagonal floor of radius 60 around a hexagonal well */
struct slab_case {
    pcg_slab_params p;
    vec2            pts[12];
    poly_ring       rings[2];
    size_t          nr_rings;
};

/* the reference layout's slab around a well of @hole_r, 0 for none */
static void slab_case_init(struct slab_case *c, float hole_r)
{
    pcg_slab_params_init(&c->p);
    ring_plate_pts(c->pts, 6, 60.0f, hole_r);
    c->rings[0] = (poly_ring){ 0, 6 };
    c->rings[1] = (poly_ring){ 6, 6 };
    c->nr_rings = hole_r > 0.0f ? 2 : 1;
}

static cerr slab_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct slab_case *c = priv;

    return pcg_slab_emit(ctx, &c->p, .frame = PCG_FRAME_IDENTITY, .pts = c->pts, .nr_pts = c->nr_rings * 6,
                         .rings = c->rings, .nr_rings = c->nr_rings);
}

static int slab_test(void)
{
    struct slab_case c;
    slab_case_init(&c, 20.0f);

    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("slab"), return EXIT_FAILURE);
    pcg_ctx ctx = { .sink = sink, .palette = &ref_palette, .frame = PCG_FRAME_IDENTITY };

    /* the defaults emit, and every surface has a role and a collide class */
    if (failed(slab_case_emit(&ctx, &c)) || !pcg_sink_get_stats(sink).nr_vertices)  return EXIT_FAILURE;
    for (int s = 0; s < PCG_SLAB_NR_SURF; s++)
        if (c.p.surf[s].collide != PCG_COLLIDE_SOLID || c.p.surf[s].skip)           return EXIT_FAILURE;
    if (c.p.finish.role != PCG_ROLE_FLOOR || c.p.soffit.role != PCG_ROLE_STRUCTURE) return EXIT_FAILURE;

    /* zeroed or nonsense style, a site with too few points */
    LOCAL_SET(pcg_sink, bad) = CRES_RET(pcg_sink_new("slab"), return EXIT_FAILURE);
    pcg_ctx bc = ctx;
    bc.sink = bad;

    struct slab_case z = c;
    z.p = (pcg_slab_params){};
    if (!failed(slab_case_emit(&bc, &z)))                                           return EXIT_FAILURE;
    z = c;
    z.p.t = -0.3f;
    if (!failed(slab_case_emit(&bc, &z)))                                           return EXIT_FAILURE;
    z.p.t = NAN;
    if (!failed(slab_case_emit(&bc, &z)))                                           return EXIT_FAILURE;
    z = c;
    z.p.finish_t = -0.05f;
    if (!failed(slab_case_emit(&bc, &z)))                                           return EXIT_FAILURE;
    /* nothing that isn't finite, even where the first plate would go out whole */
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = { &z.p.t, &z.p.finish_t, &z.pts[0][0], &z.pts[11][1] };

        for (size_t k = 0; k < array_size(fields); k++) {
            z = c;
            *fields[k] = nonfinite[i];
            if (!failed_inval(slab_case_emit(&bc, &z)))                             return EXIT_FAILURE;
        }
    }
    for (size_t i = 0; i < array_size(bad_frames); i++)
        if (!failed_inval(pcg_slab_emit(&bc, &c.p, .frame = bad_frames[i], .pts = c.pts, .nr_pts = 12,
                                        .rings = c.rings, .nr_rings = 2)))
            return EXIT_FAILURE;
    if (!failed(pcg_slab_emit(&bc, &c.p, .frame = PCG_FRAME_IDENTITY, .pts = c.pts, .nr_pts = 2)))  return EXIT_FAILURE;
    if (pcg_sink_get_stats(bad).nr_models)                                                          return EXIT_FAILURE;

    /* the box it was asked for, closed and outward under any frame, finished or not */
    struct sink_walk w;
    vec2 lo, hi;
    poly2d_bbox(c.pts, 6, lo, hi);
    if (!sink_walk_get(sink, &w))                                                   return EXIT_FAILURE;
    if (!walk_aabb_is(&w, &PCG_FRAME_IDENTITY, (vec3){ lo[0], -c.p.t, lo[1] }, (vec3){ hi[0], c.p.finish_t, hi[1] }))
        return EXIT_FAILURE;
    double vol = (ngon_area(6, 60.0) - ngon_area(6, 20.0)) * (c.p.t + c.p.finish_t);
    if (fabs(walk_volume(&w) - vol) > 1e-4 * vol)                                   return EXIT_FAILURE;
    if (!element_frames_ok(slab_case_emit, &c, &ref_palette, true))                 return EXIT_FAILURE;

    struct slab_case roof = c;
    roof.p.finish_t = 0.0f;
    if (!element_frames_ok(slab_case_emit, &roof, &ref_palette, true))              return EXIT_FAILURE;

    struct slab_case solid;
    slab_case_init(&solid, 0.0f);
    if (!element_frames_ok(slab_case_emit, &solid, &ref_palette, true))             return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int slab_surface_test(void)
{
    struct slab_case c;
    slab_case_init(&c, 24.0f);
    for (int s = 0; s < PCG_SLAB_NR_SURF; s++)  c.p.surf[s].mat = &surf_mat[s];

    for (int finished = 0; finished < 2; finished++) {
        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("slab"), return EXIT_FAILURE);
        pcg_ctx ctx = { .sink = sink, .frame = PCG_FRAME_IDENTITY };
        const pcg_frame *id = &PCG_FRAME_IDENTITY;
        float t = c.p.t, ft = finished ? 0.05f : 0.0f;

        c.p.finish_t = ft;
        if (failed(slab_case_emit(&ctx, &c)))   return EXIT_FAILURE;

        /* each surface's bucket holds its faces and nothing else; a finish buries the top */
        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != (finished ? 4u : 3u))   return EXIT_FAILURE;

        struct mesh *top = walk_mesh(&w, &surf_mat[PCG_SLAB_TOP]);
        struct mesh *soffit = walk_mesh(&w, &surf_mat[PCG_SLAB_SOFFIT]);
        struct mesh *edge = walk_mesh(&w, &surf_mat[PCG_SLAB_EDGE]);
        struct mesh *finish = walk_mesh(&w, &surf_mat[PCG_SLAB_FINISH]);
        struct mesh *finish_edge = walk_mesh(&w, &surf_mat[PCG_SLAB_FINISH_EDGE]);

        if (!soffit || !mesh_normals_along(soffit, id, (vec3){ 0.0f, -1.0f, 0.0f }))    return EXIT_FAILURE;
        if (!mesh_y_within(soffit, id, -t, -t))                                         return EXIT_FAILURE;
        if (!edge || !mesh_normals_level(edge) || !mesh_y_within(edge, id, -t, 0.0f))   return EXIT_FAILURE;
        if (finished) {
            if (top)                                                                    return EXIT_FAILURE;
            if (!finish || !mesh_normals_along(finish, id, (vec3){ 0.0f, 1.0f, 0.0f })) return EXIT_FAILURE;
            if (!mesh_y_within(finish, id, ft, ft))                                     return EXIT_FAILURE;
            if (!finish_edge || !mesh_normals_level(finish_edge))                       return EXIT_FAILURE;
            if (!mesh_y_within(finish_edge, id, 0.0f, ft))                              return EXIT_FAILURE;
        } else {
            if (finish || finish_edge)                                                  return EXIT_FAILURE;
            if (!top || !mesh_normals_along(top, id, (vec3){ 0.0f, 1.0f, 0.0f }))       return EXIT_FAILURE;
            if (!mesh_y_within(top, id, 0.0f, 0.0f))                                    return EXIT_FAILURE;
        }

        /* the structure's surfaces on one material, the finish's on another: two buckets, the unions */
        struct slab_case a = c;
        a.p.soffit.mat = a.p.edge.mat = a.p.top.mat = &concrete;
        a.p.finish.mat = a.p.finish_edge.mat = &wood;

        LOCAL_SET(pcg_sink, as) = CRES_RET(pcg_sink_new("slab"), return EXIT_FAILURE);
        pcg_ctx actx = { .sink = as, .frame = PCG_FRAME_IDENTITY };
        if (failed(slab_case_emit(&actx, &a)))  return EXIT_FAILURE;

        struct sink_walk aw;
        if (!sink_walk_get(as, &aw) || aw.nr != (finished ? 2u : 1u))   return EXIT_FAILURE;

        struct mesh *s_sep[3] = { soffit, edge, top }, *f_sep[2] = { finish, finish_edge };
        struct mesh *s_al = walk_mesh(&aw, &concrete), *f_al = walk_mesh(&aw, &wood);
        if (!meshes_same_tris(&s_al, 1, s_sep, finished ? 2 : 3))                       return EXIT_FAILURE;
        if (finished && !meshes_same_tris(&f_al, 1, f_sep, 2))                          return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*
 * The reference layout's floors against a reference copy of the plate
 * emitter the slab replaces, called the way the reference building calls
 * it: on the galleries, a 0.35 concrete slab without its top under a 0.05
 * wood deck without its bottom, around wells of 20, 24 and 28; the roof
 * slab whole, around the top well; on the ground, a 0.8 concrete plinth
 * without its top under the deck, without a well.
 */
static int slab_parity_test(void)
{
    const struct { float t, finish_t, hole_r; } cases[] = {
        { 0.35f, 0.05f, 20.0f }, { 0.35f, 0.05f, 24.0f }, { 0.35f, 0.05f, 28.0f },
        { 0.35f, 0.0f, 28.0f }, { 0.8f, 0.05f, 0.0f },
    };
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    for (size_t i = 0; i < array_size(cases); i++)
        for (int fi = 0; fi < 2; fi++) {
            const pcg_frame *f = &frames[fi];
            struct slab_case c;

            slab_case_init(&c, cases[i].hole_r);
            c.p.t = cases[i].t;
            c.p.finish_t = cases[i].finish_t;

            LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("slab"), return EXIT_FAILURE);
            pcg_ctx ctx = { .sink = sink, .palette = &ref_palette, .frame = *f };
            if (failed(slab_case_emit(&ctx, &c)))   return EXIT_FAILURE;

            struct sink_walk w;
            bool finished = c.p.finish_t > 0.0f;
            if (!sink_walk_get(sink, &w) || w.nr != (finished ? 2u : 1u))   return EXIT_FAILURE;

            int hs = cases[i].hole_r > 0.0f ? 6 : 0;
            LOCAL_SET(mesh_t, rs) = CRES_RET(ref_new_checked(mesh, .name = "ref slab"), return EXIT_FAILURE);
            if (failed(ref_plate_emit(rs, 6, 60.0f, hs, cases[i].hole_r, -c.p.t, 0.0f, REF_UV_CONCRETE,
                                      finished ? REF_PLATE_NO_TOP : 0)))
                return EXIT_FAILURE;
            if (!parity_check_exact("slab", walk_mesh(&w, &concrete), rs, f, true, REF_UV_CONCRETE))
                return EXIT_FAILURE;
            if (!finished)  continue;

            LOCAL_SET(mesh_t, rd) = CRES_RET(ref_new_checked(mesh, .name = "ref deck"), return EXIT_FAILURE);
            if (failed(ref_plate_emit(rd, 6, 60.0f, hs, cases[i].hole_r, 0.0f, c.p.finish_t, REF_UV_FLOOR,
                                      REF_PLATE_NO_BOTTOM)))
                return EXIT_FAILURE;
            if (!parity_check_exact("deck", walk_mesh(&w, &wood), rd, f, true, REF_UV_FLOOR))   return EXIT_FAILURE;
        }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Fascia
 ****************************************************************************/

/* a fascia along a path, with cuts */
struct fascia_case {
    pcg_fascia_params   p;
    vec2                pts[8];
    size_t              nr_pts;
    pcg_region          cuts[2];
    size_t              nr_cuts;
    bool                closed;
};

/* a closed hexagonal rim of @r, cut across the middle of its second edge */
static void fascia_case_init(struct fascia_case *c, float r)
{
    vec2 dir, left;

    pcg_fascia_params_init(&c->p);
    for (int j = 0; j < 6; j++) ref_ngon_vertex(j, 6, r, c->pts[j]);
    c->nr_pts = 6;
    c->closed = true;

    poly2d_edge_frame(c->pts[1], c->pts[2], dir, left);
    c->cuts[0] = (pcg_region){
        .frame  = { .org = { c->pts[1][0], 0.0f, c->pts[1][1] }, .dir = { dir[0], dir[1] } },
        .along  = { .lo = 3.0f, .hi = 4.5f },
        .across = { .lo = -1.0f, .hi = 3.0f },
    };
    c->nr_cuts = 1;
}

/* the runs @c's band breaks into, and their total length */
static size_t fascia_case_runs(const struct fascia_case *c, double *len)
{
    const pcg_span_tol tol = { .min_frac = c->p.min_frac, .merge = c->p.min_seg, .min_run = c->p.min_seg };
    size_t nr_edges = c->closed ? c->nr_pts : c->nr_pts - 1, nr_runs = 0;

    *len = 0.0;
    for (size_t i = 0; i < nr_edges; i++) {
        pcg_span runs[3];
        size_t nr = CRES_RET(pcg_segment_runs(c->pts[i], c->pts[(i + 1) % c->nr_pts], c->cuts, c->nr_cuts, &tol,
                                              runs, 3),
                             return 0);

        for (size_t k = 0; k < nr; k++) *len += runs[k].hi - runs[k].lo;
        nr_runs += nr;
    }

    return nr_runs;
}

static cerr fascia_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct fascia_case *c = priv;

    return pcg_fascia_emit(ctx, &c->p, .frame = PCG_FRAME_IDENTITY, .pts = c->pts, .nr_pts = c->nr_pts,
                           .cuts = c->cuts, .nr_cuts = c->nr_cuts, .closed = c->closed);
}

static int fascia_test(void)
{
    struct fascia_case c;
    fascia_case_init(&c, 9.0f);

    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("fascia"), return EXIT_FAILURE);
    pcg_ctx ctx = { .sink = sink, .palette = &ref_palette, .frame = PCG_FRAME_IDENTITY };

    /* the defaults emit, every surface solid structure */
    if (failed(fascia_case_emit(&ctx, &c)) || !pcg_sink_get_stats(sink).nr_vertices)    return EXIT_FAILURE;
    for (int s = 0; s < PCG_FASCIA_NR_SURF; s++)
        if (c.p.surf[s].role != PCG_ROLE_STRUCTURE || c.p.surf[s].collide != PCG_COLLIDE_SOLID ||
            c.p.surf[s].skip)
            return EXIT_FAILURE;

    /* the cut splits one edge: 7 runs, 36 vertices each, of the band's cross-section */
    double len;
    struct sink_walk w;
    if (fascia_case_runs(&c, &len) != 7)                                                return EXIT_FAILURE;
    if (!sink_walk_get(sink, &w) || w.nr != 1 || mesh_nr_vx(w.mesh[0]) != 7 * 36)       return EXIT_FAILURE;
    if (!walk_closed(&w))                                                               return EXIT_FAILURE;

    double vol = len * c.p.depth * (c.p.lip + c.p.tuck);
    if (fabs(walk_volume(&w) - vol) > 1e-4 * vol)                                       return EXIT_FAILURE;
    if (!element_frames_ok(fascia_case_emit, &c, &ref_palette, true))                   return EXIT_FAILURE;

    /* one open edge: the box it was asked for */
    struct fascia_case e = c;
    e.pts[0][0] = 1.0f;     e.pts[0][1] = 2.0f;
    e.pts[1][0] = 5.0f;     e.pts[1][1] = 2.0f;
    e.nr_pts = 2;
    e.nr_cuts = 0;
    e.closed = false;
    LOCAL_SET(pcg_sink, es) = CRES_RET(element_sink(fascia_case_emit, &e, &ref_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(es, &w) || w.nr != 1 || mesh_nr_vx(w.mesh[0]) != 36) return EXIT_FAILURE;
    if (!walk_aabb_is(&w, &PCG_FRAME_IDENTITY, (vec3){ 1.0f, -c.p.depth, 2.0f - c.p.tuck },
                      (vec3){ 5.0f, 0.0f, 2.0f + c.p.lip }))
        return EXIT_FAILURE;
    if (!element_frames_ok(fascia_case_emit, &e, &ref_palette, true))                   return EXIT_FAILURE;

    /* a cut that leaves a sliver at the edge's start: the sliver goes */
    e.cuts[0] = (pcg_region){
        .frame  = { .org = { 1.0f, 0.0f, 2.0f }, .dir = { 1.0f, 0.0f } },
        .along  = { .lo = 0.04f, .hi = 1.0f },
        .across = { .lo = -1.0f, .hi = 1.0f },
    };
    e.nr_cuts = 1;
    LOCAL_SET(pcg_sink, ss) = CRES_RET(element_sink(fascia_case_emit, &e, &ref_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(ss, &w) || w.nr != 1 || mesh_nr_vx(w.mesh[0]) != 36) return EXIT_FAILURE;
    if (!walk_aabb_is(&w, &PCG_FRAME_IDENTITY, (vec3){ 2.0f, -c.p.depth, 2.0f - c.p.tuck },
                      (vec3){ 5.0f, 0.0f, 2.0f + c.p.lip }))
        return EXIT_FAILURE;

    /* zeroed or nonsense style, paths too short or with no length to an edge, cuts missing */
    LOCAL_SET(pcg_sink, bad) = CRES_RET(pcg_sink_new("fascia"), return EXIT_FAILURE);
    pcg_ctx bc = ctx;
    bc.sink = bad;

    struct fascia_case z = c;
    z.p = (pcg_fascia_params){};
    if (!failed(fascia_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.lip = -0.06f;
    if (!failed(fascia_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.lip = z.p.tuck = 0.0f;
    if (!failed(fascia_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.min_seg = -1.0f;
    if (!failed(fascia_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.min_frac = 1.0f;
    if (!failed(fascia_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.depth = NAN;
    if (!failed(fascia_case_emit(&bc, &z)))     return EXIT_FAILURE;
    /* nothing that isn't finite, even a point past the edges that would go out first */
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = {
            &z.p.depth, &z.p.lip, &z.p.tuck, &z.p.min_seg, &z.p.min_frac, &z.pts[0][0], &z.pts[3][1],
            &z.cuts[0].along.lo, &z.cuts[0].across.hi, &z.cuts[0].frame.org[0], &z.cuts[0].frame.dir[1],
        };

        for (size_t k = 0; k < array_size(fields); k++) {
            z = c;
            *fields[k] = nonfinite[i];
            if (!failed_inval(fascia_case_emit(&bc, &z)))   return EXIT_FAILURE;
        }
    }
    for (size_t i = 0; i < array_size(bad_frames); i++)
        if (!failed_inval(pcg_fascia_emit(&bc, &c.p, .frame = bad_frames[i], .pts = c.pts, .nr_pts = 6,
                                          .cuts = c.cuts, .nr_cuts = c.nr_cuts, .closed = true)))
            return EXIT_FAILURE;
    z = c;
    z.nr_pts = 2;
    if (!failed(fascia_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    memcpy(z.pts[3], z.pts[2], sizeof(vec2));
    if (!failed(fascia_case_emit(&bc, &z)))     return EXIT_FAILURE;
    if (!failed(pcg_fascia_emit(&bc, &c.p, .frame = PCG_FRAME_IDENTITY, .pts = c.pts, .nr_pts = 6, .nr_cuts = 1)))
        return EXIT_FAILURE;
    if (pcg_sink_get_stats(bad).nr_models)      return EXIT_FAILURE;

    /* the check takes the fascia the emit takes and turns down what it turns down */
    const pcg_fascia_site site = {
        .frame = PCG_FRAME_IDENTITY, .pts = c.pts, .nr_pts = c.nr_pts, .cuts = c.cuts, .nr_cuts = c.nr_cuts,
        .closed = true,
    };
    CERR_RET(pcg_fascia_check(&c.p, &site), return EXIT_FAILURE);
    pcg_fascia_site short_site = site;
    short_site.nr_pts = 2;
    if (!failed_inval(pcg_fascia_check(&c.p, &short_site)))                 return EXIT_FAILURE;
    if (!failed_inval(pcg_fascia_check(&(pcg_fascia_params){}, &site)))     return EXIT_FAILURE;
    if (!failed_inval(pcg_fascia_check(nullptr, &site)))                    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int fascia_surface_test(void)
{
    struct fascia_case c;
    fascia_case_init(&c, 9.0f);
    for (int s = 0; s < PCG_FASCIA_NR_SURF; s++)    c.p.surf[s].mat = &surf_mat[s];

    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(fascia_case_emit, &c, nullptr, &PCG_FRAME_IDENTITY),
                                         return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(sink, &w) || w.nr != 2)  return EXIT_FAILURE;

    /* the back faces away from the void, behind the path; the face bucket has the rest */
    struct mesh *face = walk_mesh(&w, &surf_mat[PCG_FASCIA_FACE]), *back = walk_mesh(&w, &surf_mat[PCG_FASCIA_BACK]);
    if (!face || !back || mesh_nr_vx(face) != 7 * 30 || mesh_nr_vx(back) != 7 * 6)  return EXIT_FAILURE;
    if (!mesh_normals_level(back))                                                  return EXIT_FAILURE;
    for (size_t v = 0; v < mesh_nr_vx(back); v++) {
        const float *p = &mesh_vx(back)[v * 3], *n = &mesh_norm(back)[v * 3];

        /* the void is the hexagon's inside: the back faces out of it, behind the rim */
        if (!(n[0] * p[0] + n[2] * p[2] > 0.0f))                        return EXIT_FAILURE;
        if (!(sqrtf(p[0] * p[0] + p[2] * p[2]) > 9.0f * 0.8660254f))    return EXIT_FAILURE;
    }

    /* aliased, one bucket of the union; skipped, the back is gone */
    struct fascia_case a = c;
    a.p.face.mat = a.p.back.mat = &concrete;
    LOCAL_SET(pcg_sink, as) = CRES_RET(element_sink(fascia_case_emit, &a, nullptr, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    struct sink_walk aw;
    struct mesh *sep[2] = { face, back };
    if (!sink_walk_get(as, &aw) || aw.nr != 1 || !meshes_same_tris(aw.mesh, 1, sep, 2)) return EXIT_FAILURE;

    a.p.back.skip = true;
    LOCAL_SET(pcg_sink, ks) = CRES_RET(element_sink(fascia_case_emit, &a, nullptr, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(ks, &aw) || aw.nr != 1 || !meshes_same_tris(aw.mesh, 1, &face, 1))   return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/*
 * A reference copy of the layout loop the fascia replaces: one level's
 * band around a regular @n-gon rim of @rf, every edge split around its
 * crossing of the arriving flight's boarding window @win, if any, the
 * pieces over 0.05 kept
 */
static cerr ref_fascia_level(struct mesh *m, int n, float rf, const ref_window *win, float depth, float uv)
{
    for (int i = 0; i < n; i++) {
        vec2 va, vb, dir;

        ref_ngon_vertex(i, n, rf, va);
        ref_ngon_vertex(i + 1, n, rf, vb);
        dir[0] = vb[0] - va[0];
        dir[1] = vb[1] - va[1];

        float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1]);

        dir[0] /= len;
        dir[1] /= len;

        vec2 in = { -dir[1], dir[0] };
        float c0 = len, c1 = len;

        if (win) {
            float t0, t1;

            if (ref_edge_cut(va, vb, win, &t0, &t1)) {
                c0 = t0 * len;
                c1 = t1 * len;
            }
        }

        float sp[2][2] = { { 0.0f, c0 }, { c1, len } };

        for (int s = 0; s < 2; s++)
            if (sp[s][1] - sp[s][0] > 0.05f)
                CERR_RET_CERR(ref_fascia_seg(m, va, dir, in, sp[s][0], sp[s][1], depth, uv));
    }

    return CERR_OK;
}

/*
 * The reference building's fascias, levels 1 to 4 of a 6-sided well of
 * radius 20, 24, 28 and 28, 0.9 deep, each but the roof's broken over the
 * flight arriving there: 50 risers of 0.12 and 0.20 goings, 5 m wide 0.03
 * off the chord, cut where a 3.2 m walker on the treads below would meet
 * the band and the 0.05 deck over it, with 0.1 m to spare
 */
static int fascia_parity_test(void)
{
    const float well_r[] = { 20.0f, 24.0f, 28.0f, 28.0f };
    const int n = 6, floors = 4, steps = 50;
    const float H = 6.0f, going = 0.20f, agent_h = 3.2f, depth = 0.9f, deck_t = 0.05f, stair_w = 5.0f;
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    for (int f = 1; f <= floors; f++)
        for (int fi = 0; fi < 2; fi++) {
            const pcg_frame *frame = &frames[fi];
            float rf = well_r[f - 1];
            bool windowed = f - 1 < floors - 1;
            ref_window win;
            pcg_region reg = {};

            if (windowed) {
                int fl = f - 1;
                vec2 B;

                ref_window_calc(&win, n, well_r[(fl ? fl : 1) - 1], well_r[fl], fl);
                ref_ngon_vertex(2 * fl + 1, n, well_r[fl], B);

                float dx = B[0] - win.A[0], dz = B[1] - win.A[1];
                float len = sqrtf(dx * dx + dz * dz);
                float run = steps * going, land = (len - run) * 0.5f;
                float slope = H / steps / going;

                win.al0 = len - land - (agent_h + depth + deck_t + 0.1f) / slope;
                win.al1 = 1e9f;
                win.acmax = 0.03f + stair_w;
                ref_window_region(&win, &reg);
            }

            struct fascia_case c;
            pcg_fascia_params_init(&c.p);
            c.p.depth = depth;
            for (int j = 0; j < n; j++) ref_ngon_vertex(j, n, rf, c.pts[j]);
            c.nr_pts = n;
            c.closed = true;
            c.cuts[0] = reg;
            c.nr_cuts = windowed;

            /* the window does take a piece out of the band: a regular hexagon's side is its radius */
            double len;
            fascia_case_runs(&c, &len);
            if (windowed != (len < 6.0 * rf - 1.0)) return EXIT_FAILURE;

            LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(fascia_case_emit, &c, &ref_palette, frame),
                                                 return EXIT_FAILURE);
            struct sink_walk w;
            if (!sink_walk_get(sink, &w) || w.nr != 1)  return EXIT_FAILURE;

            LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref fascia"), return EXIT_FAILURE);
            if (failed(ref_fascia_level(ref, n, rf, windowed ? &win : nullptr, depth, REF_UV_CONCRETE)))
                return EXIT_FAILURE;
            if (!parity_check("fascia", walk_mesh(&w, &concrete), ref, frame, false, REF_UV_CONCRETE))
                return EXIT_FAILURE;
        }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Columns
 ****************************************************************************/

/*
 * Reference copies of the emitters the column replaces: an axis-aligned
 * box with planar UVs in its own coordinates at @uv repeats per meter,
 * and a shaft on an optional base block built of two of them
 */
static cerr ref_arch_box(struct mesh *m, vec3 lo, vec3 hi, float uv)
{
    CRES_RET_CERR(_prim_begin(36, &(prim_emit_opts){ .mesh = m, .tangents = true }));

    float x0 = lo[0], y0 = lo[1], z0 = lo[2];
    float x1 = hi[0], y1 = hi[1], z1 = hi[2];

    ref_face(m, (vec3){ x1, y0, z0 }, (vec3){ x1, y0, z1 }, (vec3){ x1, y1, z1 },
             (vec3){ x1, y1, z0 }, z0 * uv, y0 * uv, z1 * uv, y1 * uv);
    ref_face(m, (vec3){ x0, y0, z1 }, (vec3){ x0, y0, z0 }, (vec3){ x0, y1, z0 },
             (vec3){ x0, y1, z1 }, -z1 * uv, y0 * uv, -z0 * uv, y1 * uv);
    ref_face(m, (vec3){ x0, y1, z0 }, (vec3){ x1, y1, z0 }, (vec3){ x1, y1, z1 },
             (vec3){ x0, y1, z1 }, x0 * uv, z0 * uv, x1 * uv, z1 * uv);
    ref_face(m, (vec3){ x0, y0, z0 }, (vec3){ x0, y0, z1 }, (vec3){ x1, y0, z1 },
             (vec3){ x1, y0, z0 }, z0 * uv, x0 * uv, z1 * uv, x1 * uv);
    ref_face(m, (vec3){ x1, y0, z1 }, (vec3){ x0, y0, z1 }, (vec3){ x0, y1, z1 },
             (vec3){ x1, y1, z1 }, -x1 * uv, y0 * uv, -x0 * uv, y1 * uv);
    ref_face(m, (vec3){ x0, y0, z0 }, (vec3){ x1, y0, z0 }, (vec3){ x1, y1, z0 },
             (vec3){ x0, y1, z0 }, x0 * uv, y0 * uv, x1 * uv, y1 * uv);

    return CERR_OK;
}

typedef struct ref_column_params {
    float   w, h;
    float   base_w, base_h;
    float   uv;
} ref_column_params;

static cerr ref_column_emit(struct mesh *m, const ref_column_params *p)
{
    float hw = p->w * 0.5f;

    CERR_RET_CERR(ref_arch_box(m, (vec3){ -hw, 0.0f, -hw }, (vec3){ hw, p->h, hw }, p->uv));

    if (p->base_w > 0.0f && p->base_h > 0.0f) {
        float hb = p->base_w * 0.5f;

        CERR_RET_CERR(ref_arch_box(m, (vec3){ -hb, 0.0f, -hb }, (vec3){ hb, p->base_h, hb }, p->uv));
    }

    return CERR_OK;
}

/*
 * @src's triangles moved by @at into @dst: the original placed one model
 * per column, and its entity's position moves the vertices at draw time
 */
static cerr ref_instance(struct mesh *dst, struct mesh *src, const vec3 at)
{
    size_t nr = mesh_nr_vx(src);

    CRES_RET_CERR(_prim_begin(nr, &(prim_emit_opts){ .mesh = dst, .tangents = true }));
    for (size_t v = 0; v + 2 < nr; v += 3) {
        vec3 p[3];
        vec2 uv[3];

        for (int k = 0; k < 3; k++) {
            vec3_add(p[k], &mesh_vx(src)[(v + k) * 3], at);
            memcpy(uv[k], &mesh_tx(src)[(v + k) * 2], sizeof(vec2));
        }
        _prim_emit_triangle3(p[0], p[1], p[2], &(prim_emit_opts){ .mesh = dst, .uv = uv });
    }

    return CERR_OK;
}

/* a set of columns, or one at the origin */
struct column_case {
    pcg_column_params   p;
    vec2                at[8];
    size_t              nr_at;
    float               h;
};

static cerr column_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct column_case *c = priv;

    if (!c->nr_at)  return pcg_column_emit(ctx, &c->p, .frame = PCG_FRAME_IDENTITY, .h = c->h);

    return pcg_columns_emit(ctx, &c->p, .frame = PCG_FRAME_IDENTITY, .at = c->at, .nr_at = c->nr_at, .h = c->h);
}

static int column_test(void)
{
    struct column_case c = { .h = 3.0f };
    pcg_column_params_init(&c.p);

    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(column_case_emit, &c, &ref_palette, &PCG_FRAME_IDENTITY),
                                         return EXIT_FAILURE);
    if (c.p.shaft.role != PCG_ROLE_CLADDING || c.p.base.role != PCG_ROLE_STRUCTURE) return EXIT_FAILURE;
    for (int s = 0; s < PCG_COLUMN_NR_SURF; s++)
        if (c.p.surf[s].collide != PCG_COLLIDE_SOLID || c.p.surf[s].skip)           return EXIT_FAILURE;

    /* the shaft stops short of the site's top by the gap, on its base */
    struct sink_walk w;
    float shaft_h = c.h - c.p.top_gap, hb = c.p.base_w * 0.5f;
    if (!sink_walk_get(sink, &w) || w.nr != 2 || walk_nr_vx(&w) != 72)  return EXIT_FAILURE;
    if (!walk_aabb_is(&w, &PCG_FRAME_IDENTITY, (vec3){ -hb, 0.0f, -hb }, (vec3){ hb, shaft_h, hb }))
        return EXIT_FAILURE;
    if (!walk_closed(&w))                                                               return EXIT_FAILURE;
    double vol = (double)c.p.w * c.p.w * shaft_h + (double)c.p.base_w * c.p.base_w * c.p.base_h;
    if (fabs(walk_volume(&w) - vol) > 1e-4 * vol)                                       return EXIT_FAILURE;
    if (!element_frames_ok(column_case_emit, &c, &ref_palette, true))                   return EXIT_FAILURE;

    /* without a base, a bare shaft */
    struct column_case bare = c;
    bare.p.base_w = bare.p.base_h = 0.0f;
    LOCAL_SET(pcg_sink, bs) = CRES_RET(element_sink(column_case_emit, &bare, &ref_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(bs, &w) || w.nr != 1 || walk_nr_vx(&w) != 36)    return EXIT_FAILURE;

    /* a colonnade: every column in the same two buckets, each where it was put */
    struct column_case row = c;
    row.nr_at = 5;
    for (size_t i = 0; i < row.nr_at; i++) {
        row.at[i][0] = 3.0f * i;
        row.at[i][1] = i & 1 ? 1.0f : -1.0f;
    }
    LOCAL_SET(pcg_sink, rs) = CRES_RET(element_sink(column_case_emit, &row, &ref_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(rs, &w) || w.nr != 2 || walk_nr_vx(&w) != 5 * 72)    return EXIT_FAILURE;
    if (!walk_aabb_is(&w, &PCG_FRAME_IDENTITY, (vec3){ -hb, 0.0f, -1.0f - hb },
                      (vec3){ 12.0f + hb, shaft_h, 1.0f + hb }))
        return EXIT_FAILURE;
    if (fabs(walk_volume(&w) - 5.0 * vol) > 1e-4 * vol)                                 return EXIT_FAILURE;
    if (!element_frames_ok(column_case_emit, &row, &ref_palette, true))                 return EXIT_FAILURE;

    /* zeroed or nonsense style, a site too low for its gap or its base, no feet */
    LOCAL_SET(pcg_sink, bad) = CRES_RET(pcg_sink_new("column"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bad, .palette = &ref_palette, .frame = PCG_FRAME_IDENTITY };
    struct column_case z = c;
    z.p = (pcg_column_params){};
    if (!failed(column_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.w = NAN;
    if (!failed(column_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.base_w = 0.3f;
    if (!failed(column_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.base_h = 0.0f;
    if (!failed(column_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.base_w = 0.0f;
    if (!failed(column_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.top_gap = -0.02f;
    if (!failed(column_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.h = 0.02f;
    if (!failed(column_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.h = 0.45f;
    if (!failed(column_case_emit(&bc, &z)))     return EXIT_FAILURE;
    /* nothing that isn't finite, on one column or a row, not even the last foot */
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = { &z.p.w, &z.p.base_w, &z.p.base_h, &z.p.top_gap, &z.h };

        for (size_t k = 0; k < array_size(fields); k++) {
            z = c;
            *fields[k] = nonfinite[i];
            if (!failed_inval(column_case_emit(&bc, &z)))   return EXIT_FAILURE;
            z = row;
            *fields[k] = nonfinite[i];
            if (!failed_inval(column_case_emit(&bc, &z)))   return EXIT_FAILURE;
        }
        z = row;
        z.at[row.nr_at - 1][1] = nonfinite[i];
        if (!failed_inval(column_case_emit(&bc, &z)))       return EXIT_FAILURE;
    }
    for (size_t i = 0; i < array_size(bad_frames); i++)
        if (!failed_inval(pcg_column_emit(&bc, &c.p, .frame = bad_frames[i], .h = c.h)) ||
            !failed_inval(pcg_columns_emit(&bc, &c.p, .frame = bad_frames[i], .at = row.at, .nr_at = row.nr_at,
                                           .h = c.h)))
            return EXIT_FAILURE;
    if (!failed(pcg_columns_emit(&bc, &c.p, .frame = PCG_FRAME_IDENTITY, .at = c.at, .h = c.h)))    return EXIT_FAILURE;
    if (pcg_sink_get_stats(bad).nr_models)                                                          return EXIT_FAILURE;

    /* the check takes the column the emit takes and turns down what it turns down */
    const pcg_column_site site = { .frame = PCG_FRAME_IDENTITY, .h = c.h };
    CERR_RET(pcg_column_check(&c.p, &site), return EXIT_FAILURE);
    if (!failed_inval(pcg_column_check(&c.p, &(pcg_column_site){ .frame = PCG_FRAME_IDENTITY, .h = 0.02f })))
        return EXIT_FAILURE;
    if (!failed_inval(pcg_column_check(&(pcg_column_params){}, &site)))     return EXIT_FAILURE;
    if (!failed_inval(pcg_column_check(nullptr, &site)))                    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int column_surface_test(void)
{
    struct column_case c = { .h = 3.0f, .nr_at = 3, .at = { { 0.0f, 0.0f }, { 2.0f, 0.0f }, { 4.0f, 1.0f } } };
    pcg_column_params_init(&c.p);
    for (int s = 0; s < PCG_COLUMN_NR_SURF; s++)    c.p.surf[s].mat = &surf_mat[s];

    /* each surface's bucket holds its own box of every column */
    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(column_case_emit, &c, nullptr, &PCG_FRAME_IDENTITY),
                                         return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(sink, &w) || w.nr != 2)  return EXIT_FAILURE;

    struct mesh *shaft = walk_mesh(&w, &surf_mat[PCG_COLUMN_SHAFT]), *base = walk_mesh(&w, &surf_mat[PCG_COLUMN_BASE]);
    float hw = c.p.w * 0.5f, hb = c.p.base_w * 0.5f;
    if (!shaft || !base || mesh_nr_vx(shaft) != 3 * 36 || mesh_nr_vx(base) != 3 * 36)   return EXIT_FAILURE;

    struct sink_walk sw = { .nr = 1, .mesh = { shaft } }, bw = { .nr = 1, .mesh = { base } };
    if (!walk_aabb_is(&sw, &PCG_FRAME_IDENTITY, (vec3){ -hw, 0.0f, -hw },
                      (vec3){ 4.0f + hw, c.h - c.p.top_gap, 1.0f + hw }))
        return EXIT_FAILURE;
    if (!walk_aabb_is(&bw, &PCG_FRAME_IDENTITY, (vec3){ -hb, 0.0f, -hb }, (vec3){ 4.0f + hb, c.p.base_h, 1.0f + hb }))
        return EXIT_FAILURE;

    /* aliased, one bucket of the union */
    struct column_case a = c;
    a.p.shaft.mat = a.p.base.mat = &panel;
    LOCAL_SET(pcg_sink, as) = CRES_RET(element_sink(column_case_emit, &a, nullptr, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    struct mesh *sep[2] = { shaft, base };
    if (!sink_walk_get(as, &w) || w.nr != 1 || !meshes_same_tris(w.mesh, 1, sep, 2))    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/*
 * The reference building's well columns, against a reference copy of the
 * column emitter they replace and of their placement: 0.5 m panel-clad
 * shafts on 0.675 m bases 0.55 m tall, one at every vertex of a 6-sided
 * ring per storey of 6 m, standing on the 0.05 deck and stopping 0.02 m
 * short of the next one, so the cap buries in the slab above; the ring
 * is the wider of the storey's two wells, of radius 20, 24, 28 and 28
 */
static int column_parity_test(void)
{
    const float well_r[] = { 20.0f, 24.0f, 28.0f, 28.0f };
    const int n = 6, floors = 4;
    const float H = 6.0f, deck_t = 0.05f, column_w = 0.5f;
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    LOCAL_SET(mesh_t, col) = CRES_RET(ref_new_checked(mesh, .name = "ref column"), return EXIT_FAILURE);
    CERR_RET(ref_column_emit(col, &(ref_column_params){
                 .w = column_w, .h = H - deck_t - 0.02f,
                 .base_w = column_w * 1.35f, .base_h = 0.55f, .uv = REF_UV_PANEL,
             }),
             return EXIT_FAILURE);

    struct column_case c = { .nr_at = n, .h = H - deck_t };
    pcg_column_params_init(&c.p);
    c.p.w = column_w;
    c.p.base_w = column_w * 1.35f;
    c.p.base_h = 0.55f;
    c.p.top_gap = 0.02f;
    c.p.base.role = PCG_ROLE_CLADDING;

    for (int fi = 0; fi < 2; fi++) {
        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("column"), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref ring"), return EXIT_FAILURE);
        pcg_ctx ctx = { .sink = sink, .palette = &ref_palette, .frame = frames[fi] };

        for (int s = 0; s < floors; s++) {
            float rc = s ? fmaxf(well_r[s - 1], well_r[s]) : well_r[0];
            float y = s * H + deck_t;

            for (int j = 0; j < n; j++) {
                ref_ngon_vertex(j, n, rc, c.at[j]);
                CERR_RET(ref_instance(ref, col, (vec3){ c.at[j][0], y, c.at[j][1] }), return EXIT_FAILURE);
            }

            CERR_RET(pcg_columns_emit(&ctx, &c.p, .frame = { .org = { 0.0f, y, 0.0f }, .dir = { 1.0f, 0.0f } },
                                      .at = c.at, .nr_at = c.nr_at, .h = c.h),
                     return EXIT_FAILURE);
        }

        /* 24 columns, one bucket */
        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != 1)                                          return EXIT_FAILURE;
        if (!parity_check_exact("columns", walk_mesh(&w, &panel), ref, &frames[fi], false, REF_UV_PANEL))
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Balustrade
 ****************************************************************************/

/* the reference layout's rail finish, and a separate one for the rods of sloped runs */
static const pcg_material slat = { .name = "slat", .uv_mode = PCG_UV_METERS };
static const pcg_material rod = { .name = "rod", .uv_mode = PCG_UV_METERS };
static const pcg_palette rail_palette = {
    .mat[PCG_ROLE_STRUCTURE]    = &concrete,
    .mat[PCG_ROLE_FLOOR]        = &wood,
    .mat[PCG_ROLE_CLADDING]     = &panel,
    .mat[PCG_ROLE_RAIL]         = &slat,
};

#define REF_UV_SLAT (1.0f / 1.4f)

/* a straight run without points, else a path with cuts */
struct balustrade_case {
    pcg_balustrade_params   p;
    float                   len;
    float                   rise;
    float                   y;
    float                   inset;
    vec2                    pts[8];
    size_t                  nr_pts;
    pcg_region              cuts[2];
    size_t                  nr_cuts;
    bool                    closed;
};

static cerr balustrade_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct balustrade_case *c = priv;

    if (!c->nr_pts)
        return pcg_balustrade_emit(ctx, &c->p, .frame = PCG_FRAME_IDENTITY, .len = c->len, .rise = c->rise,
                                   .y = c->y);

    return pcg_balustrade_path_emit(ctx, &c->p, .frame = PCG_FRAME_IDENTITY, .pts = c->pts, .nr_pts = c->nr_pts,
                                    .cuts = c->cuts, .nr_cuts = c->nr_cuts, .y = c->y, .inset = c->inset,
                                    .closed = c->closed);
}

/* the check of what balustrade_case_emit() emits */
static cerr balustrade_case_check(const struct balustrade_case *c)
{
    if (!c->nr_pts) {
        const pcg_balustrade_site site = { .frame = PCG_FRAME_IDENTITY, .len = c->len, .rise = c->rise, .y = c->y };

        return pcg_balustrade_check(&c->p, &site);
    }

    const pcg_balustrade_path path = {
        .frame = PCG_FRAME_IDENTITY, .pts = c->pts, .nr_pts = c->nr_pts, .cuts = c->cuts, .nr_cuts = c->nr_cuts,
        .y = c->y, .inset = c->inset, .closed = c->closed,
    };

    return pcg_balustrade_path_check(&c->p, &path);
}

/* the slats of a run of @len */
static int balustrade_slats(const pcg_balustrade_params *p, float len)
{
    int n = (int)roundf(len / (p->slat_w + p->gap));

    return n < 1 ? 1 : n;
}

/* the vertices of a run of @len */
static size_t balustrade_nr_vx(const pcg_balustrade_params *p, float len, float rise)
{
    return (size_t)balustrade_slats(p, len) * 36 + (rise == 0.0f ? 2 * 36 : 2 * 4 * 12);
}

/* the volume of a run of @len, each member counted whole */
static double balustrade_volume(const pcg_balustrade_params *p, float len, float rise)
{
    double slat_h = (double)p->h - p->cap_t - p->shoe_h + 2.0 * p->slat_overlap;
    double vol = balustrade_slats(p, len) * (double)p->slat_w * p->slat_t * slat_h;

    if (rise == 0.0f)   return vol + (double)len * ((double)p->shoe_h * p->shoe_w + (double)p->cap_t * p->rail_w);

    /* a square of circumradius r has an area of 2 * r^2 */
    double l = sqrt((double)len * len + (double)rise * rise);

    return vol + 2.0 * l * ((double)p->slope_shoe_r * p->slope_shoe_r + (double)p->slope_cap_r * p->slope_cap_r);
}

/* the vertices and volume of @c's path */
static size_t balustrade_case_nr_vx(const struct balustrade_case *c, double *vol)
{
    const pcg_span_tol tol = { .min_frac = c->p.min_frac, .merge = c->p.merge, .min_run = c->p.min_run };
    size_t nr_edges = c->closed ? c->nr_pts : c->nr_pts - 1, nr_vx = 0;

    *vol = 0.0;
    for (size_t i = 0; i < nr_edges; i++) {
        pcg_span runs[3];
        size_t nr = CRES_RET(pcg_segment_runs(c->pts[i], c->pts[(i + 1) % c->nr_pts], c->cuts, c->nr_cuts, &tol,
                                              runs, 3),
                             return 0);

        for (size_t k = 0; k < nr; k++) {
            nr_vx += balustrade_nr_vx(&c->p, runs[k].hi - runs[k].lo, 0.0f);
            *vol += balustrade_volume(&c->p, runs[k].hi - runs[k].lo, 0.0f);
        }
    }

    return nr_vx;
}

static int balustrade_test(void)
{
    struct balustrade_case c = { .len = 3.0f };
    pcg_balustrade_params_init(&c.p);

    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(balustrade_case_emit, &c, &rail_palette, &PCG_FRAME_IDENTITY),
                                         return EXIT_FAILURE);
    for (int s = 0; s < PCG_BALUSTRADE_NR_SURF; s++)
        if (c.p.surf[s].role != PCG_ROLE_RAIL || c.p.surf[s].collide != PCG_COLLIDE_HAZARD || c.p.surf[s].skip)
            return EXIT_FAILURE;

    /* a level run: shoe, cap and 21 slats, the cap the widest */
    struct sink_walk w;
    float hw = c.p.rail_w * 0.5f;
    if (balustrade_slats(&c.p, c.len) != 21)                                            return EXIT_FAILURE;
    if (!sink_walk_get(sink, &w) || w.nr != 1)                                          return EXIT_FAILURE;
    if (walk_nr_vx(&w) != balustrade_nr_vx(&c.p, c.len, 0.0f))                          return EXIT_FAILURE;
    if (!walk_aabb_is(&w, &PCG_FRAME_IDENTITY, (vec3){ 0.0f, 0.0f, -hw }, (vec3){ c.len, c.p.h, hw }))
        return EXIT_FAILURE;
    if (!walk_closed(&w))                                                               return EXIT_FAILURE;
    double vol = balustrade_volume(&c.p, c.len, 0.0f);
    if (fabs(walk_volume(&w) - vol) > 1e-4 * vol)                                       return EXIT_FAILURE;
    if (!element_frames_ok(balustrade_case_emit, &c, &rail_palette, true))              return EXIT_FAILURE;

    /* standing higher in its frame */
    struct balustrade_case up = c;
    up.y = 2.0f;
    LOCAL_SET(pcg_sink, us) = CRES_RET(element_sink(balustrade_case_emit, &up, &rail_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(us, &w) || w.nr != 1)                                            return EXIT_FAILURE;
    if (!walk_aabb_is(&w, &PCG_FRAME_IDENTITY, (vec3){ 0.0f, 2.0f, -hw }, (vec3){ c.len, 2.0f + c.p.h, hw }))
        return EXIT_FAILURE;

    /* sloped: rods for the shoe and the cap, 29 slats */
    struct balustrade_case s = c;
    s.len = 4.0f;
    s.rise = 2.4f;
    LOCAL_SET(pcg_sink, ss) = CRES_RET(element_sink(balustrade_case_emit, &s, &rail_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (balustrade_slats(&s.p, s.len) != 29)                                            return EXIT_FAILURE;
    if (!sink_walk_get(ss, &w) || w.nr != 1 || !walk_closed(&w))                        return EXIT_FAILURE;
    if (walk_nr_vx(&w) != balustrade_nr_vx(&s.p, s.len, s.rise))                        return EXIT_FAILURE;
    vol = balustrade_volume(&s.p, s.len, s.rise);
    if (fabs(walk_volume(&w) - vol) > 1e-4 * vol)                                       return EXIT_FAILURE;
    if (!element_frames_ok(balustrade_case_emit, &s, &rail_palette, true))              return EXIT_FAILURE;

    /* every slat stands on the climb: its foot rises with its middle */
    s.p.shoe.skip = s.p.cap.skip = true;
    LOCAL_SET(pcg_sink, sl) = CRES_RET(element_sink(balustrade_case_emit, &s, &rail_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(sl, &w) || w.nr != 1 || mesh_nr_vx(w.mesh[0]) != 29 * 36)    return EXIT_FAILURE;
    for (size_t v = 0; v < mesh_nr_vx(w.mesh[0]); v += 36) {
        const float *vx = mesh_vx(w.mesh[0]);
        vec3 lo, hi;

        /* the slat's own box: its 36 vertices */
        for (int k = 0; k < 3; k++) {
            lo[k] = hi[k] = vx[v * 3 + k];
            for (size_t u = v; u < v + 36; u++) {
                lo[k] = fminf(lo[k], vx[u * 3 + k]);
                hi[k] = fmaxf(hi[k], vx[u * 3 + k]);
            }
        }

        float base = s.rise * (lo[0] + hi[0]) * 0.5f / s.len;
        if (!near(lo[1], base + s.p.shoe_h - s.p.slat_overlap, 1e-5f))          return EXIT_FAILURE;
        if (!near(hi[1], base + s.p.h - s.p.cap_t + s.p.slat_overlap, 1e-5f))   return EXIT_FAILURE;
    }

    /* shorter than a pitch: one slat */
    struct balustrade_case sh = c;
    sh.len = 0.05f;
    LOCAL_SET(pcg_sink, hs) = CRES_RET(element_sink(balustrade_case_emit, &sh, &rail_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(hs, &w) || walk_nr_vx(&w) != 3 * 36) return EXIT_FAILURE;

    /* zeroed or nonsense style, a run without length or with a rise that isn't finite */
    LOCAL_SET(pcg_sink, bad) = CRES_RET(pcg_sink_new("balustrade"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bad, .palette = &rail_palette, .frame = PCG_FRAME_IDENTITY };
    struct balustrade_case z = c;
    z.p = (pcg_balustrade_params){};
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.h = NAN;
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.h = INFINITY;
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.slat_w = INFINITY;
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.h = 0.15f;
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.gap = -0.01f;
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.slat_overlap = -0.01f;
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.slope_cap_r = 0.0f;
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.merge = -1.0f;
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.min_frac = 1.0f;
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.len = 0.0f;
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.rise = INFINITY;
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    for (size_t i = 0; i < array_size(bad_frames); i++)
        if (!failed_inval(pcg_balustrade_emit(&bc, &c.p, .frame = bad_frames[i], .len = c.len, .rise = c.rise,
                                              .y = c.y)))
            return EXIT_FAILURE;
    if (pcg_sink_get_stats(bad).nr_models)          return EXIT_FAILURE;

    /* the check takes the run the emit takes and turns down what it turns down */
    CERR_RET(balustrade_case_check(&c), return EXIT_FAILURE);
    z = c;
    z.len = 0.0f;
    if (!failed_inval(balustrade_case_check(&z)))                           return EXIT_FAILURE;
    z = c;
    z.p = (pcg_balustrade_params){};
    if (!failed_inval(balustrade_case_check(&z)))                           return EXIT_FAILURE;
    if (!failed_inval(pcg_balustrade_check(&c.p, nullptr)))                 return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* a closed hexagonal rim of @r, cut across the middle of its second edge, the rail 0.09 back from it */
static void balustrade_path_init(struct balustrade_case *c, float r)
{
    vec2 dir, left;

    *c = (struct balustrade_case){ .nr_pts = 6, .closed = true, .inset = 0.09f };
    pcg_balustrade_params_init(&c->p);
    for (int j = 0; j < 6; j++) ref_ngon_vertex(j, 6, r, c->pts[j]);

    poly2d_edge_frame(c->pts[1], c->pts[2], dir, left);
    c->cuts[0] = (pcg_region){
        .frame  = { .org = { c->pts[1][0], 0.0f, c->pts[1][1] }, .dir = { dir[0], dir[1] } },
        .along  = { .lo = 3.0f, .hi = 4.5f },
        .across = { .lo = -1.0f, .hi = 3.0f },
    };
    c->nr_cuts = 1;
}

static int balustrade_path_test(void)
{
    const float r = 9.0f;
    struct balustrade_case c;
    balustrade_path_init(&c, r);

    /* every run of the path, one edge split in two */
    double vol;
    size_t nr_vx = balustrade_case_nr_vx(&c, &vol);
    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(balustrade_case_emit, &c, &rail_palette, &PCG_FRAME_IDENTITY),
                                         return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(sink, &w) || w.nr != 1 || walk_nr_vx(&w) != nr_vx)               return EXIT_FAILURE;
    if (!walk_closed(&w) || fabs(walk_volume(&w) - vol) > 1e-4 * vol)                   return EXIT_FAILURE;
    if (!element_frames_ok(balustrade_case_emit, &c, &rail_palette, true))              return EXIT_FAILURE;

    struct balustrade_case whole = c;
    double whole_vol;
    whole.nr_cuts = 0;
    balustrade_case_nr_vx(&whole, &whole_vol);
    if (!(whole_vol > vol))                                                             return EXIT_FAILURE;

    /* the rail stands back from the rim, every member within half the cap's width of its line */
    float apothem = r * cosf((float)M_PI / 6.0f);
    for (size_t v = 0; v < mesh_nr_vx(w.mesh[0]); v++) {
        const float *p = &mesh_vx(w.mesh[0])[v * 3];
        float out = -INFINITY;

        for (int j = 0; j < 6; j++) {
            vec2 dir, left;

            poly2d_edge_frame(c.pts[j], c.pts[(j + 1) % 6], dir, left);
            out = fmaxf(out, -(p[0] * left[0] + p[2] * left[1]));
        }
        if (!(out > apothem + c.inset - c.p.rail_w * 0.5f - 1e-4f)) return EXIT_FAILURE;
    }

    /* one open edge: one run, moved back from it */
    struct balustrade_case e = c;
    e.pts[0][0] = 1.0f;     e.pts[0][1] = 2.0f;
    e.pts[1][0] = 5.0f;     e.pts[1][1] = 2.0f;
    e.nr_pts = 2;
    e.nr_cuts = 0;
    e.closed = false;
    float hw = c.p.rail_w * 0.5f, z0 = 2.0f - e.inset;
    LOCAL_SET(pcg_sink, es) = CRES_RET(element_sink(balustrade_case_emit, &e, &rail_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(es, &w) || walk_nr_vx(&w) != balustrade_nr_vx(&e.p, 4.0f, 0.0f)) return EXIT_FAILURE;
    if (!walk_aabb_is(&w, &PCG_FRAME_IDENTITY, (vec3){ 1.0f, 0.0f, z0 - hw }, (vec3){ 5.0f, c.p.h, z0 + hw }))
        return EXIT_FAILURE;

    /* a cut leaving a sliver at the start: the sliver goes */
    e.cuts[0] = (pcg_region){
        .frame  = { .org = { 1.0f, 0.0f, 2.0f }, .dir = { 1.0f, 0.0f } },
        .along  = { .lo = 0.1f, .hi = 1.0f },
        .across = { .lo = -1.0f, .hi = 1.0f },
    };
    e.nr_cuts = 1;
    LOCAL_SET(pcg_sink, ls) = CRES_RET(element_sink(balustrade_case_emit, &e, &rail_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(ls, &w) || walk_nr_vx(&w) != balustrade_nr_vx(&e.p, 3.0f, 0.0f)) return EXIT_FAILURE;
    if (!walk_aabb_is(&w, &PCG_FRAME_IDENTITY, (vec3){ 2.0f, 0.0f, z0 - hw }, (vec3){ 5.0f, c.p.h, z0 + hw }))
        return EXIT_FAILURE;

    /* two cuts closer than the merge distance: one gap, two runs */
    e.cuts[0].along = (pcg_range){ .lo = 1.0f, .hi = 1.5f };
    e.cuts[1] = e.cuts[0];
    e.cuts[1].along = (pcg_range){ .lo = 1.53f, .hi = 2.0f };
    e.nr_cuts = 2;
    LOCAL_SET(pcg_sink, ms) = CRES_RET(element_sink(balustrade_case_emit, &e, &rail_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(ms, &w))                                                                     return EXIT_FAILURE;
    if (walk_nr_vx(&w) != balustrade_nr_vx(&e.p, 1.0f, 0.0f) + balustrade_nr_vx(&e.p, 2.0f, 0.0f))  return EXIT_FAILURE;

    /* paths too short, an edge with no length, cuts missing, an inset that isn't finite */
    LOCAL_SET(pcg_sink, bad) = CRES_RET(pcg_sink_new("balustrade"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bad, .palette = &rail_palette, .frame = PCG_FRAME_IDENTITY };
    struct balustrade_case z = c;
    z.nr_pts = 2;
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = e;
    z.nr_pts = 1;
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    memcpy(z.pts[3], z.pts[2], sizeof(vec2));
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.inset = NAN;
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.min_run = -0.15f;
    if (!failed(balustrade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    /* nothing that isn't finite, even a point or a cut past the edges that would go out first */
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = {
            &z.pts[0][0], &z.pts[3][1], &z.cuts[0].along.hi, &z.cuts[0].across.lo, &z.cuts[0].frame.org[2],
            &z.cuts[0].frame.dir[0],
        };

        for (size_t k = 0; k < array_size(fields); k++) {
            z = c;
            *fields[k] = nonfinite[i];
            if (!failed_inval(balustrade_case_emit(&bc, &z)))   return EXIT_FAILURE;
        }
    }
    for (size_t i = 0; i < array_size(bad_frames); i++)
        if (!failed_inval(pcg_balustrade_path_emit(&bc, &c.p, .frame = bad_frames[i], .pts = c.pts,
                                                   .nr_pts = c.nr_pts, .cuts = c.cuts, .nr_cuts = c.nr_cuts,
                                                   .inset = c.inset, .closed = c.closed)))
            return EXIT_FAILURE;
    if (!failed(pcg_balustrade_path_emit(&bc, &c.p, .frame = PCG_FRAME_IDENTITY, .pts = c.pts, .nr_pts = 6,
                                         .nr_cuts = 1, .closed = true)))
        return EXIT_FAILURE;
    if (pcg_sink_get_stats(bad).nr_models)          return EXIT_FAILURE;

    /* the check takes the path the emit takes and turns down what it turns down */
    CERR_RET(balustrade_case_check(&c), return EXIT_FAILURE);
    z = c;
    memcpy(z.pts[3], z.pts[2], sizeof(vec2));
    if (!failed_inval(balustrade_case_check(&z)))                           return EXIT_FAILURE;
    z = c;
    z.nr_pts = 2;
    if (!failed_inval(balustrade_case_check(&z)))                           return EXIT_FAILURE;
    if (!failed_inval(pcg_balustrade_path_check(&c.p, nullptr)))            return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* the largest U of @m */
static float mesh_max_u(struct mesh *m)
{
    float u = -INFINITY;

    for (size_t v = 0; v < mesh_nr_vx(m); v++)  u = fmaxf(u, mesh_tx(m)[v * 2]);

    return u;
}

static int balustrade_surface_test(void)
{
    struct balustrade_case c = { .len = 3.0f };
    pcg_balustrade_params_init(&c.p);
    for (int s = 0; s < PCG_BALUSTRADE_NR_SURF; s++)    c.p.surf[s].mat = &surf_mat[s];

    /* each surface's bucket holds its own members */
    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(balustrade_case_emit, &c, nullptr, &PCG_FRAME_IDENTITY),
                                         return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(sink, &w) || w.nr != 3)  return EXIT_FAILURE;

    const pcg_frame *id = &PCG_FRAME_IDENTITY;
    struct mesh *cap = walk_mesh(&w, &surf_mat[PCG_BALUSTRADE_CAP]);
    struct mesh *shoe = walk_mesh(&w, &surf_mat[PCG_BALUSTRADE_SHOE]);
    struct mesh *infill = walk_mesh(&w, &surf_mat[PCG_BALUSTRADE_INFILL]);
    if (!cap || mesh_nr_vx(cap) != 36 || !mesh_y_within(cap, id, c.p.h - c.p.cap_t, c.p.h))     return EXIT_FAILURE;
    if (!shoe || mesh_nr_vx(shoe) != 36 || !mesh_y_within(shoe, id, 0.0f, c.p.shoe_h))          return EXIT_FAILURE;
    if (!infill || mesh_nr_vx(infill) != 21 * 36)                                               return EXIT_FAILURE;
    if (!mesh_y_within(infill, id, c.p.shoe_h - c.p.slat_overlap, c.p.h - c.p.cap_t + c.p.slat_overlap))
        return EXIT_FAILURE;

    /* aliased, one bucket of the union; skipped, the infill is gone */
    struct balustrade_case a = c;
    a.p.cap.mat = a.p.shoe.mat = a.p.infill.mat = &slat;
    LOCAL_SET(pcg_sink, as) = CRES_RET(element_sink(balustrade_case_emit, &a, nullptr, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    struct sink_walk aw;
    struct mesh *sep[3] = { cap, shoe, infill };
    if (!sink_walk_get(as, &aw) || aw.nr != 1 || !meshes_same_tris(aw.mesh, 1, sep, 3)) return EXIT_FAILURE;

    a.p.infill.skip = true;
    LOCAL_SET(pcg_sink, ks) = CRES_RET(element_sink(balustrade_case_emit, &a, nullptr, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(ks, &aw) || aw.nr != 1 || !meshes_same_tris(aw.mesh, 1, sep, 2)) return EXIT_FAILURE;

    /* sloped, the rods chart meters along their length, with tangents */
    struct balustrade_case s = c;
    s.len = 4.0f;
    s.rise = 3.0f;
    pcg_frame f;
    test_frame(&f, 1.1f, false, 3.0f, 2.0f, -1.0f);
    LOCAL_SET(pcg_sink, ss) = CRES_RET(element_sink(balustrade_case_emit, &s, nullptr, &f), return EXIT_FAILURE);
    if (!sink_walk_get(ss, &w) || w.nr != 3)    return EXIT_FAILURE;

    cap = walk_mesh(&w, &surf_mat[PCG_BALUSTRADE_CAP]);
    shoe = walk_mesh(&w, &surf_mat[PCG_BALUSTRADE_SHOE]);
    if (!cap || mesh_nr_vx(cap) != 48 || !shoe || mesh_nr_vx(shoe) != 48)                       return EXIT_FAILURE;
    if (!near(mesh_max_u(cap), 5.0f, 1e-5f) || !near(mesh_max_u(shoe), 5.0f, 1e-5f))            return EXIT_FAILURE;
    if (!mesh_tangents_ok(cap) || !mesh_tangents_ok(shoe))                                      return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/*
 * Reference copies of the run frame box and the balustrade run they
 * replace, at @uv repeats per meter. The run sends its sloped shoe and
 * cap prisms to @rods, with tangents, instead of @m: their charts are the
 * accepted change, and the parity helpers read tangents. A level run
 * sends its shoe and cap boxes to @boxes instead of @m, if given.
 */
static cerr ref_dir_box(struct mesh *m, const vec2 org, const vec2 dir, float a0, float a1, float y0, float y1,
                        float c0, float c1, float uv)
{
    if (a0 > a1) { float t = a0; a0 = a1; a1 = t; }

    float px = -dir[1], pz = dir[0];
    CRES_RET_CERR(_prim_begin(36, &(prim_emit_opts){ .mesh = m, .tangents = true }));

#define DIRPT(_a, _y, _c) \
    (vec3){ org[0] + (_a) * dir[0] + (_c) * px, (_y), org[1] + (_a) * dir[1] + (_c) * pz }
    ref_face(m, DIRPT(a1, y0, c0), DIRPT(a1, y0, c1), DIRPT(a1, y1, c1), DIRPT(a1, y1, c0),
             c0 * uv, y0 * uv, c1 * uv, y1 * uv);
    ref_face(m, DIRPT(a0, y0, c1), DIRPT(a0, y0, c0), DIRPT(a0, y1, c0), DIRPT(a0, y1, c1),
             -c1 * uv, y0 * uv, -c0 * uv, y1 * uv);
    ref_face(m, DIRPT(a0, y1, c0), DIRPT(a1, y1, c0), DIRPT(a1, y1, c1), DIRPT(a0, y1, c1),
             a0 * uv, c0 * uv, a1 * uv, c1 * uv);
    ref_face(m, DIRPT(a0, y0, c0), DIRPT(a0, y0, c1), DIRPT(a1, y0, c1), DIRPT(a1, y0, c0),
             c0 * uv, a0 * uv, c1 * uv, a1 * uv);
    ref_face(m, DIRPT(a1, y0, c1), DIRPT(a0, y0, c1), DIRPT(a0, y1, c1), DIRPT(a1, y1, c1),
             -a1 * uv, y0 * uv, -a0 * uv, y1 * uv);
    ref_face(m, DIRPT(a0, y0, c0), DIRPT(a1, y0, c0), DIRPT(a1, y1, c0), DIRPT(a0, y1, c0),
             a0 * uv, y0 * uv, a1 * uv, y1 * uv);
#undef DIRPT

    return CERR_OK;
}

typedef struct ref_balustrade_params {
    vec2    org, dir;
    float   len, rise;
    float   h;
    float   slat_w, slat_t, gap;
    float   shoe_h;
    float   rail_w;
    float   y0;
    float   uv;
    struct mesh *boxes;
} ref_balustrade_params;

static cerr ref_balustrade_emit(struct mesh *m, struct mesh *rods, const ref_balustrade_params *p)
{
    float cap_t = 0.07f;
    prim_emit_opts opts = { .mesh = rods, .tangents = true };

    if (p->rise == 0.0f) {
        struct mesh *bm = p->boxes ? p->boxes : m;

        CERR_RET_CERR(ref_dir_box(bm, p->org, p->dir, 0.0f, p->len, p->y0, p->y0 + p->shoe_h,
                                  -p->slat_t * 0.9f, p->slat_t * 0.9f, p->uv));
        CERR_RET_CERR(ref_dir_box(bm, p->org, p->dir, 0.0f, p->len, p->y0 + p->h - cap_t, p->y0 + p->h,
                                  -p->rail_w * 0.5f, p->rail_w * 0.5f, p->uv));
    } else {
        vec3 a = { p->org[0], p->y0 + p->shoe_h * 0.5f, p->org[1] };
        vec3 b = { p->org[0] + p->len * p->dir[0], p->y0 + p->rise + p->shoe_h * 0.5f,
                   p->org[1] + p->len * p->dir[1] };

        CERR_RET_CERR(_prim_emit_prism(a, b, p->shoe_h * 0.7f, p->shoe_h * 0.7f, 4, (float)M_PI / 4.0f, &opts));

        a[1] = p->y0 + p->h - cap_t * 0.5f;
        b[1] = p->y0 + p->rise + p->h - cap_t * 0.5f;
        CERR_RET_CERR(_prim_emit_prism(a, b, p->rail_w * 0.6f, p->rail_w * 0.6f, 4, (float)M_PI / 4.0f, &opts));
    }

    int n = (int)roundf(p->len / (p->slat_w + p->gap));
    if (n < 1)  n = 1;
    float pitch = p->len / n;

    for (int i = 0; i < n; i++) {
        float at = (i + 0.5f) * pitch;
        float base = p->y0 + p->rise * at / p->len;

        CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, at - p->slat_w * 0.5f, at + p->slat_w * 0.5f,
                                  base + p->shoe_h - 0.02f, base + p->h - cap_t + 0.02f,
                                  -p->slat_t * 0.5f, p->slat_t * 0.5f, p->uv));
    }

    return CERR_OK;
}

/* the reference layout's rail: 1.05 tall, 0.09 by 0.045 slats 0.05 apart, a 0.10 shoe and cap */
static void ref_rail_init(ref_balustrade_params *p, const vec2 dir)
{
    *p = (ref_balustrade_params){
        .dir = { dir[0], dir[1] }, .h = 1.05f,
        .slat_w = 0.09f, .slat_t = 0.045f, .gap = 0.05f,
        .shoe_h = 0.10f, .rail_w = 0.10f, .uv = REF_UV_SLAT,
    };
}

/*
 * A reference copy of the layout loop the path replaces: one level's rail
 * around a regular @n-gon rim of @rf, 0.09 back from it, every edge cut by
 * the boarding windows @win and split into its runs (ref_rim_runs()); the
 * total length of the runs goes to @len
 */
static cerr ref_rim_level(struct mesh *m, int n, float rf, const ref_window *win, int nr_win, float *len)
{
    float bal_off = 0.09f;

    *len = 0.0f;
    for (int i = 0; i < n; i++) {
        vec2 va, vb, dir;

        ref_ngon_vertex(i, n, rf, va);
        ref_ngon_vertex(i + 1, n, rf, vb);
        dir[0] = vb[0] - va[0];
        dir[1] = vb[1] - va[1];

        float l = sqrtf(dir[0] * dir[0] + dir[1] * dir[1]);

        dir[0] /= l;
        dir[1] /= l;

        vec2 in = { -dir[1], dir[0] };
        float runs[3][2];
        int nr = ref_rim_runs(va, vb, win, nr_win, runs);

        for (int k = 0; k < nr; k++) {
            float at = runs[k][0], end = runs[k][1];
            ref_balustrade_params bp;

            ref_rail_init(&bp, dir);
            bp.org[0] = va[0] - in[0] * bal_off + dir[0] * at;
            bp.org[1] = va[1] - in[1] * bal_off + dir[1] * at;
            bp.len = end - at;
            CERR_RET_CERR(ref_balustrade_emit(m, nullptr, &bp));
            *len += bp.len;
        }
    }

    return CERR_OK;
}

/* a reference copy of a flight's chord geometry: its window frame, chord length, landing and run */
typedef struct ref_flight {
    ref_window  w;
    float       len;
    float       land;
    float       run;
} ref_flight;

static void ref_flight_calc(ref_flight *g, const float *well_r, int n, int f, int steps, float going)
{
    vec2 B;

    ref_window_calc(&g->w, n, well_r[(f ? f : 1) - 1], well_r[f], f);
    ref_ngon_vertex(2 * f + 1, n, well_r[f], B);

    float dx = B[0] - g->w.A[0], dz = B[1] - g->w.A[1];

    g->len = sqrtf(dx * dx + dz * dz);
    g->run = steps * going;
    g->land = (g->len - g->run) * 0.5f;
}

/* a reference copy of the along-extent of an @n-gon of @r at @across in @g's frame; <= 0 when outside */
static float ref_poly_span_frame(int n, float r, const ref_flight *g, float across, float *lo, float *hi)
{
    float mn = 0.0f, mx = 0.0f;
    bool any = false;

    for (int j = 0; j < n; j++) {
        vec2 a, b;

        ref_ngon_vertex(j, n, r, a);
        ref_ngon_vertex(j + 1, n, r, b);

        float ca = (a[0] - g->w.A[0]) * g->w.in[0] + (a[1] - g->w.A[1]) * g->w.in[1];
        float cb = (b[0] - g->w.A[0]) * g->w.in[0] + (b[1] - g->w.A[1]) * g->w.in[1];

        if ((ca <= across) == (cb <= across))   continue;

        float t = (across - ca) / (cb - ca);
        float al = (a[0] - g->w.A[0]) * g->w.dir[0] + (a[1] - g->w.A[1]) * g->w.dir[1];
        float bl = (b[0] - g->w.A[0]) * g->w.dir[0] + (b[1] - g->w.A[1]) * g->w.dir[1];
        float x = al + (bl - al) * t;

        if (!any) { mn = mx = x; any = true; }
        else if (x < mn)    mn = x;
        else if (x > mx)    mx = x;
    }

    *lo = mn;
    *hi = mx;

    return any ? mx - mn : 0.0f;
}

/* the reference building's measures the rails and flights depend on */
#define REF_FLOORS      4
#define REF_WELL_SIDES  6
#define REF_FLOOR_H     6.0f
#define REF_DECK_T      0.05f
#define REF_RAIL_H      1.05f
#define REF_STEPS       50
#define REF_GOING       0.20f
#define REF_STAIR_W     5.0f
#define REF_AGENT_H     3.2f

static const float ref_well_r[] = { 20.0f, 24.0f, 28.0f, 28.0f };

/* a flight's run frame at (@a, @c) of its chord, on the floor @y */
static void ref_flight_frame(const ref_flight *g, float a, float c, float y, pcg_frame *f)
{
    *f = (pcg_frame){
        .org = { g->w.A[0] + g->w.dir[0] * a + g->w.in[0] * c, y, g->w.A[1] + g->w.dir[1] * a + g->w.in[1] * c },
        .dir = { g->w.dir[0], g->w.dir[1] },
    };
}

/*
 * The reference building's gallery rails at levels 1 to 3 around a
 * 6-sided well of radius 20, 24 and 28, standing on the 0.05 deck of 6 m
 * storeys 0.09 back from the rim, open over the boarding windows of the
 * flights touching each level: 50 risers of 0.12 and 0.20 goings, 5 m
 * wide 0.03 off the chord. The departing flight's window reaches from
 * behind its first riser to where its soffit clears the cap by the waist,
 * a riser and 0.1 m of air; the arriving flight's from where a 3.2 m
 * walker on its treads clears the deck with 0.1 m to spare, on past its
 * landing. Against a reference copy of the run and the per-level loop.
 */
static int balustrade_parity_rim(const pcg_frame *frame)
{
    const int n = REF_WELL_SIDES, nr_flights = REF_FLOORS - 1;
    const float H = REF_FLOOR_H;
    pcg_balustrade_params p;
    pcg_balustrade_params_init(&p);

    for (int f = 1; f < REF_FLOORS; f++) {
        float y = f * H + REF_DECK_T, rf = ref_well_r[f - 1];
        ref_window win[2];
        pcg_region reg[2];
        int nr_win = 0;

        for (int e = 0; e < 2; e++) {
            int fl = f - 1 + e;
            ref_flight g;

            if (fl < 0 || fl >= nr_flights) continue;
            ref_flight_calc(&g, ref_well_r, n, fl, REF_STEPS, REF_GOING);
            win[nr_win] = g.w;

            if (e) {
                win[nr_win].al0 = -1e9f;
                win[nr_win].al1 = g.land + g.run * (REF_RAIL_H + 0.22f + H / REF_STEPS + 0.1f) / H;
            } else {
                float slope = H / REF_STEPS / REF_GOING;

                win[nr_win].al0 = g.len - g.land - (REF_AGENT_H + 0.1f) / slope;
                win[nr_win].al1 = 1e9f;
            }
            win[nr_win].acmax = 0.03f + REF_STAIR_W;
            ref_window_region(&win[nr_win], &reg[nr_win]);
            nr_win++;
        }

        vec2 pts[REF_WELL_SIDES];
        for (int j = 0; j < n; j++) ref_ngon_vertex(j, n, rf, pts[j]);

        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("rim"), return EXIT_FAILURE);
        pcg_ctx ctx = { .sink = sink, .palette = &rail_palette, .frame = *frame };
        CERR_RET(pcg_balustrade_path_emit(&ctx, &p, .frame = { .org = { 0.0f, y, 0.0f }, .dir = { 1.0f, 0.0f } },
                                          .pts = pts, .nr_pts = n, .cuts = reg, .nr_cuts = nr_win, .inset = 0.09f,
                                          .closed = true),
                 return EXIT_FAILURE);

        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != 1)                      return EXIT_FAILURE;

        /* the windows do take pieces out: a regular hexagon's side is its radius */
        float len;
        LOCAL_SET(mesh_t, lv) = CRES_RET(ref_new_checked(mesh, .name = "ref rim"), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref rim placed"), return EXIT_FAILURE);
        if (failed(ref_rim_level(lv, n, rf, win, nr_win, &len)))                        return EXIT_FAILURE;
        if (!(len < n * rf - 1.0f))                                                     return EXIT_FAILURE;
        if (failed(ref_instance(ref, lv, (vec3){ 0.0f, y, 0.0f })))                     return EXIT_FAILURE;
        if (!parity_check("rim", walk_mesh(&w, &slat), ref, frame, false, REF_UV_SLAT)) return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*
 * The reference building's flight rails: for each of the 3 flights, the
 * sloped pair over the run, 0.06 and 5.06 in from the chord, and the level
 * rails along the well side of the landings, from 0.06 past the rim
 * crossing of the rail line to the run, the arrival one standing a storey
 * up in the flight's frame. The sloped shoe and cap are checked for shape
 * alone: their charts and tangents are the accepted change.
 */
static int balustrade_parity_flight(const pcg_frame *frame)
{
    const int n = REF_WELL_SIDES;
    const float H = REF_FLOOR_H, crail = 0.03f + REF_STAIR_W + 0.03f;
    pcg_balustrade_params lp, sp;
    int nr_landings = 0;

    pcg_balustrade_params_init(&lp);
    sp = lp;
    sp.cap.mat = sp.shoe.mat = &rod;

    for (int f = 0; f < REF_FLOORS - 1; f++) {
        float y = f * H + REF_DECK_T;
        ref_flight g;
        ref_balustrade_params bp;
        pcg_frame rf;

        ref_flight_calc(&g, ref_well_r, n, f, REF_STEPS, REF_GOING);
        ref_rail_init(&bp, g.w.dir);

        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("flight rail"), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, lm) = CRES_RET(ref_new_checked(mesh, .name = "ref rail"), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, lr) = CRES_RET(ref_new_checked(mesh, .name = "ref rods"), return EXIT_FAILURE);
        pcg_ctx ctx = { .sink = sink, .palette = &rail_palette, .frame = *frame };

        /* the sloped pair, rim side then well side */
        const float side[] = { 0.03f + 0.03f, crail };
        bp.len = g.run;
        bp.rise = H;
        for (int k = 0; k < 2; k++) {
            bp.org[0] = g.w.A[0] + g.w.dir[0] * g.land + g.w.in[0] * side[k];
            bp.org[1] = g.w.A[1] + g.w.dir[1] * g.land + g.w.in[1] * side[k];
            if (failed(ref_balustrade_emit(lm, lr, &bp)))       return EXIT_FAILURE;

            ref_flight_frame(&g, g.land, side[k], y, &rf);
            CERR_RET(pcg_balustrade_emit(&ctx, &sp, .frame = rf, .len = g.run, .rise = H), return EXIT_FAILURE);
        }

        /* the landings' level rails */
        float b0, b1;
        bp.rise = 0.0f;
        if (f && ref_poly_span_frame(n, ref_well_r[f - 1], &g, crail, &b0, &b1) > 0.0f) {
            bp.len = g.land - (b0 + 0.06f);
            if (bp.len > 0.15f) {
                bp.org[0] = g.w.A[0] + g.w.dir[0] * (b0 + 0.06f) + g.w.in[0] * crail;
                bp.org[1] = g.w.A[1] + g.w.dir[1] * (b0 + 0.06f) + g.w.in[1] * crail;
                if (failed(ref_balustrade_emit(lm, lr, &bp)))   return EXIT_FAILURE;

                ref_flight_frame(&g, b0 + 0.06f, crail, y, &rf);
                CERR_RET(pcg_balustrade_emit(&ctx, &lp, .frame = rf, .len = bp.len), return EXIT_FAILURE);
                nr_landings++;
            }
        }
        if (ref_poly_span_frame(n, ref_well_r[f], &g, crail, &b0, &b1) > 0.0f) {
            bp.y0 = H;
            bp.len = b1 - 0.06f - (g.len - g.land);
            if (bp.len > 0.15f) {
                bp.org[0] = g.w.A[0] + g.w.dir[0] * (g.len - g.land) + g.w.in[0] * crail;
                bp.org[1] = g.w.A[1] + g.w.dir[1] * (g.len - g.land) + g.w.in[1] * crail;
                if (failed(ref_balustrade_emit(lm, lr, &bp)))   return EXIT_FAILURE;

                ref_flight_frame(&g, g.len - g.land, crail, y, &rf);
                CERR_RET(pcg_balustrade_emit(&ctx, &lp, .frame = rf, .len = bp.len, .y = H), return EXIT_FAILURE);
                nr_landings++;
            }
        }

        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != 2)                  return EXIT_FAILURE;

        LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref rail placed"), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, rods) = CRES_RET(ref_new_checked(mesh, .name = "ref rods placed"), return EXIT_FAILURE);
        if (failed(ref_instance(ref, lm, (vec3){ 0.0f, y, 0.0f })))                             return EXIT_FAILURE;
        if (failed(ref_instance(rods, lr, (vec3){ 0.0f, y, 0.0f })))                            return EXIT_FAILURE;
        if (!parity_check("flight rail", walk_mesh(&w, &slat), ref, frame, false, REF_UV_SLAT)) return EXIT_FAILURE;
        if (!parity_check_shape("flight rail rods", walk_mesh(&w, &rod), rods, frame))          return EXIT_FAILURE;
    }

    /* landing rails were there to compare */
    if (nr_landings < 2)    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int balustrade_parity_test(void)
{
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    for (int fi = 0; fi < 2; fi++) {
        if (balustrade_parity_rim(&frames[fi]))     return EXIT_FAILURE;
        if (balustrade_parity_flight(&frames[fi]))  return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Stair
 ****************************************************************************/

/* pcg_stair_steps() fails with @code */
static bool steps_failed(const pcg_stair_params *p, float rise, cerr_enum code)
{
    cres(int) res = pcg_stair_steps(p, rise);

    return IS_CERR(res) && res.err == code;
}

static int stair_steps_test(void)
{
    pcg_stair_params p;
    pcg_stair_params_init(&p);

    /* the reference building's storey: 6 m at 0.12, 50 risers */
    p.riser_max = 0.12f;
    if (CRES_RET(pcg_stair_steps(&p, 6.0f), return EXIT_FAILURE) != 50)         return EXIT_FAILURE;

    /* the float quotient rounds past a count that fits, or onto one that doesn't */
    p.riser_max = 0.17f;
    if (!(1.19f / p.riser_max > 7.0f))                                          return EXIT_FAILURE;
    if (CRES_RET(pcg_stair_steps(&p, 1.19f), return EXIT_FAILURE) != 7)         return EXIT_FAILURE;
    p.riser_max = 0.12f;
    if (!(3.96f / p.riser_max <= 33.0f) || 3.96f / 33 <= p.riser_max)           return EXIT_FAILURE;
    if (CRES_RET(pcg_stair_steps(&p, 3.96f), return EXIT_FAILURE) != 34)        return EXIT_FAILURE;

    /* a hair less takes another riser; up to 6 / 49 still takes 50 */
    p.riser_max = nextafterf(0.12f, 0.0f);
    if (CRES_RET(pcg_stair_steps(&p, 6.0f), return EXIT_FAILURE) != 51)         return EXIT_FAILURE;
    p.riser_max = nextafterf(6.0f / 49.0f, 0.0f);
    if (CRES_RET(pcg_stair_steps(&p, 6.0f), return EXIT_FAILURE) != 50)         return EXIT_FAILURE;
    p.riser_max = 6.0f / 49.0f;
    if (CRES_RET(pcg_stair_steps(&p, 6.0f), return EXIT_FAILURE) != 49)         return EXIT_FAILURE;

    /* exact multiples and climbs under one riser */
    p.riser_max = 0.17f;
    if (CRES_RET(pcg_stair_steps(&p, 1.7f), return EXIT_FAILURE) != 10)         return EXIT_FAILURE;
    if (CRES_RET(pcg_stair_steps(&p, 3.0f), return EXIT_FAILURE) != 18)         return EXIT_FAILURE;
    if (CRES_RET(pcg_stair_steps(&p, 0.1f), return EXIT_FAILURE) != 1)          return EXIT_FAILURE;

    /* always the smallest count that fits, by the float test itself */
    for (int i = 1; i < 400; i++)
        for (int j = 1; j < 40; j++) {
            float rise = i * 0.037f, rm = 0.05f + j * 0.0071f;
            p.riser_max = rm;

            int n = CRES_RET(pcg_stair_steps(&p, rise), return EXIT_FAILURE);
            if (!(rise / n <= rm))                                              return EXIT_FAILURE;
            if (n > 1 && rise / (n - 1) <= rm)                                  return EXIT_FAILURE;
        }

    /* the run is the risers' goings */
    p.riser_max = 0.12f;
    p.going = 0.20f;
    float run = CRES_RET(pcg_stair_run(&p, 6.0f), return EXIT_FAILURE);
    if (run != 50 * 0.20f)                                                      return EXIT_FAILURE;

    /* no climb, no riser, nothing finite, too many to count */
    const cerr_enum inval = _CERR_INVALID_ARGUMENTS;
    const float bad_rise[] = { 0.0f, -1.0f, NAN, INFINITY };
    for (size_t i = 0; i < array_size(bad_rise); i++)
        if (!steps_failed(&p, bad_rise[i], inval))                              return EXIT_FAILURE;
    p.riser_max = 0.0f;
    if (!steps_failed(&p, 6.0f, inval))                                         return EXIT_FAILURE;
    cres(float) res = pcg_stair_run(&p, 6.0f);
    if (!IS_CERR_CODE(res, CERR_INVALID_ARGUMENTS))                             return EXIT_FAILURE;
    p.riser_max = INFINITY;
    if (!steps_failed(&p, 6.0f, inval))                                         return EXIT_FAILURE;
    p.riser_max = 1e-7f;
    if (!steps_failed(&p, 6.0f, _CERR_TOO_LARGE))                               return EXIT_FAILURE;
    if (!steps_failed(nullptr, 6.0f, inval))                                    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* a flight with its landings */
struct stair_case {
    pcg_stair_params    p;
    float               rise;
    float               land_lo;
    float               land_hi;
};

static cerr stair_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct stair_case *c = priv;

    return pcg_stair_emit(ctx, &c->p, .frame = PCG_FRAME_IDENTITY, .rise = c->rise, .land_lo = c->land_lo,
                          .land_hi = c->land_hi);
}

/* the volume of @c's flight, each box counted whole */
static double stair_volume(const struct stair_case *c, int steps)
{
    const pcg_stair_params *p = &c->p;
    double rs = (double)(c->rise / steps);

    return steps * (double)p->going * p->width * (rs + p->waist) +
           ((double)c->land_lo + c->land_hi) * p->landing_t * p->width;
}

static int stair_test(void)
{
    struct stair_case c = { .rise = 3.0f, .land_lo = 1.0f, .land_hi = 1.2f };
    pcg_stair_params_init(&c.p);

    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(stair_case_emit, &c, &ref_palette, &PCG_FRAME_IDENTITY),
                                         return EXIT_FAILURE);
    for (int s = 0; s < PCG_STAIR_NR_SURF; s++)
        if (c.p.surf[s].role != PCG_ROLE_STRUCTURE || c.p.surf[s].collide != PCG_COLLIDE_SOLID || c.p.surf[s].skip)
            return EXIT_FAILURE;

    /* 18 risers of 1/6 m and two landings, from behind the first riser to past the last tread */
    int steps = CRES_RET(pcg_stair_steps(&c.p, c.rise), return EXIT_FAILURE);
    float run = CRES_RET(pcg_stair_run(&c.p, c.rise), return EXIT_FAILURE), hw = c.p.width * 0.5f;
    struct sink_walk w;
    if (steps != 18 || !near(run, 18 * 0.28f, 1e-6f))                                           return EXIT_FAILURE;
    if (!sink_walk_get(sink, &w) || w.nr != 1 || walk_nr_vx(&w) != (size_t)(steps + 2) * 36)    return EXIT_FAILURE;
    if (!walk_aabb_is(&w, &PCG_FRAME_IDENTITY, (vec3){ -c.land_lo, -c.p.landing_t, -hw },
                      (vec3){ run + c.land_hi, c.rise, hw }))
        return EXIT_FAILURE;
    if (!walk_closed(&w))                                                               return EXIT_FAILURE;
    double vol = stair_volume(&c, steps);
    if (fabs(walk_volume(&w) - vol) > 1e-4 * vol)                                       return EXIT_FAILURE;
    if (!element_frames_ok(stair_case_emit, &c, &ref_palette, true))                    return EXIT_FAILURE;

    /* without landings, the steps alone, the first one's waist below the floor */
    struct stair_case bare = c;
    bare.land_lo = bare.land_hi = 0.0f;
    LOCAL_SET(pcg_sink, bs) = CRES_RET(element_sink(stair_case_emit, &bare, &ref_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(bs, &w) || walk_nr_vx(&w) != (size_t)steps * 36) return EXIT_FAILURE;
    if (!walk_aabb_is(&w, &PCG_FRAME_IDENTITY, (vec3){ 0.0f, -c.p.waist, -hw }, (vec3){ run, c.rise, hw }))
        return EXIT_FAILURE;
    vol = stair_volume(&bare, steps);
    if (!walk_closed(&w) || fabs(walk_volume(&w) - vol) > 1e-4 * vol)                   return EXIT_FAILURE;
    if (!element_frames_ok(stair_case_emit, &bare, &ref_palette, true))                 return EXIT_FAILURE;

    /* zeroed or nonsense style, a negative landing, no climb */
    LOCAL_SET(pcg_sink, bad) = CRES_RET(pcg_sink_new("stair"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bad, .palette = &ref_palette, .frame = PCG_FRAME_IDENTITY };
    struct stair_case z = c;
    z.p = (pcg_stair_params){};
    if (!failed(stair_case_emit(&bc, &z)))      return EXIT_FAILURE;
    z = c;
    z.p.width = NAN;
    if (!failed(stair_case_emit(&bc, &z)))      return EXIT_FAILURE;
    z = c;
    z.p.width = INFINITY;
    if (!failed(stair_case_emit(&bc, &z)))      return EXIT_FAILURE;
    z = c;
    z.p.going = INFINITY;
    if (!failed(stair_case_emit(&bc, &z)))      return EXIT_FAILURE;
    z = c;
    z.p.waist = 0.0f;
    if (!failed(stair_case_emit(&bc, &z)))      return EXIT_FAILURE;
    z = c;
    z.p.landing_t = -0.2f;
    if (!failed(stair_case_emit(&bc, &z)))      return EXIT_FAILURE;
    z = c;
    z.p.riser_max = 0.0f;
    if (!failed(stair_case_emit(&bc, &z)))      return EXIT_FAILURE;
    z = c;
    z.land_hi = -1.0f;
    if (!failed(stair_case_emit(&bc, &z)))      return EXIT_FAILURE;
    z = c;
    z.land_lo = INFINITY;
    if (!failed(stair_case_emit(&bc, &z)))      return EXIT_FAILURE;
    z = c;
    z.rise = 0.0f;
    if (!failed(stair_case_emit(&bc, &z)))      return EXIT_FAILURE;
    for (size_t i = 0; i < array_size(bad_frames); i++)
        if (!failed_inval(pcg_stair_emit(&bc, &c.p, .frame = bad_frames[i], .rise = c.rise, .land_lo = c.land_lo,
                                         .land_hi = c.land_hi)))
            return EXIT_FAILURE;
    if (pcg_sink_get_stats(bad).nr_models)      return EXIT_FAILURE;

    /* the check takes the flight the emit takes and turns down what it turns down */
    const pcg_stair_site site = {
        .frame = PCG_FRAME_IDENTITY, .rise = c.rise, .land_lo = c.land_lo, .land_hi = c.land_hi,
    };
    CERR_RET(pcg_stair_check(&c.p, &site), return EXIT_FAILURE);
    pcg_stair_site flat = site;
    flat.rise = 0.0f;
    if (!failed_inval(pcg_stair_check(&c.p, &flat)))                        return EXIT_FAILURE;
    if (!failed_inval(pcg_stair_check(&(pcg_stair_params){}, &site)))       return EXIT_FAILURE;
    if (!failed_inval(pcg_stair_check(&c.p, nullptr)))                      return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int stair_surface_test(void)
{
    struct stair_case c = { .rise = 3.0f, .land_lo = 1.0f, .land_hi = 1.2f };
    pcg_stair_params_init(&c.p);
    for (int s = 0; s < PCG_STAIR_NR_SURF; s++) c.p.surf[s].mat = &surf_mat[s];

    int steps = CRES_RET(pcg_stair_steps(&c.p, c.rise), return EXIT_FAILURE);
    float rs = c.rise / steps;

    /* each surface's bucket holds its faces of every step, under a yawed and mirrored frame too */
    for (int m = 0; m < 2; m++) {
        pcg_frame f = PCG_FRAME_IDENTITY;
        if (m)  test_frame(&f, 2.2f, true, -3.0f, 4.0f, 7.0f);

        LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(stair_case_emit, &c, nullptr, &f), return EXIT_FAILURE);
        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != PCG_STAIR_NR_SURF)  return EXIT_FAILURE;

        struct mesh *tread = walk_mesh(&w, &surf_mat[PCG_STAIR_TREAD]);
        struct mesh *riser = walk_mesh(&w, &surf_mat[PCG_STAIR_RISER]);
        struct mesh *side = walk_mesh(&w, &surf_mat[PCG_STAIR_SIDE]);
        struct mesh *soffit = walk_mesh(&w, &surf_mat[PCG_STAIR_SOFFIT]);
        struct mesh *landing = walk_mesh(&w, &surf_mat[PCG_STAIR_LANDING]);
        const vec3 up = { 0.0f, 1.0f, 0.0f }, down = { 0.0f, -1.0f, 0.0f };
        const vec3 fore = { 1.0f, 0.0f, 0.0f }, back = { -1.0f, 0.0f, 0.0f };
        const vec3 left = { 0.0f, 0.0f, 1.0f }, right = { 0.0f, 0.0f, -1.0f };

        size_t nr = (size_t)steps * 6;
        if (!tread || mesh_nr_vx(tread) != nr || !mesh_normals_along(tread, &f, up))    return EXIT_FAILURE;
        if (!riser || mesh_nr_vx(riser) != nr || !mesh_normals_along(riser, &f, back))  return EXIT_FAILURE;
        if (!side || mesh_nr_vx(side) != 2 * nr)                                        return EXIT_FAILURE;
        if (mesh_count_normals(side, &f, left) != nr)                                   return EXIT_FAILURE;
        if (mesh_count_normals(side, &f, right) != nr)                                  return EXIT_FAILURE;
        if (!soffit || mesh_nr_vx(soffit) != 2 * nr)                                    return EXIT_FAILURE;
        if (mesh_count_normals(soffit, &f, fore) != nr)                                 return EXIT_FAILURE;
        if (mesh_count_normals(soffit, &f, down) != nr)                                 return EXIT_FAILURE;
        if (!landing || mesh_nr_vx(landing) != 2 * 36)                                  return EXIT_FAILURE;

        /* every tread at its riser's height: rise / steps apart */
        for (size_t v = 0; v < mesh_nr_vx(tread); v++) {
            vec3 p;

            pcg_frame_point_to_local(&f, &mesh_vx(tread)[v * 3], p);
            float k = floorf(p[0] / c.p.going + 0.5f);
            if (!near(p[1], (k + 1.0f) * rs, 1e-4f) && !near(p[1], k * rs, 1e-4f))  return EXIT_FAILURE;
        }

        /* aliased, one bucket of the union */
        struct stair_case a = c;
        for (int s = 0; s < PCG_STAIR_NR_SURF; s++) a.p.surf[s].mat = &concrete;
        LOCAL_SET(pcg_sink, as) = CRES_RET(element_sink(stair_case_emit, &a, nullptr, &f), return EXIT_FAILURE);
        struct sink_walk aw;
        struct mesh *sep[PCG_STAIR_NR_SURF] = { tread, riser, side, soffit, landing };
        if (!sink_walk_get(as, &aw) || aw.nr != 1 || !meshes_same_tris(aw.mesh, 1, sep, PCG_STAIR_NR_SURF))
            return EXIT_FAILURE;

        /* skipped, the treads are gone */
        a.p.tread.skip = true;
        LOCAL_SET(pcg_sink, ks) = CRES_RET(element_sink(stair_case_emit, &a, nullptr, &f), return EXIT_FAILURE);
        if (!sink_walk_get(ks, &aw) || aw.nr != 1 || !meshes_same_tris(aw.mesh, 1, &sep[1], PCG_STAIR_NR_SURF - 1))
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/* a reference copy of the flight emitter the stair replaces */
typedef struct ref_stair_params {
    vec2    org, dir;
    float   width;
    float   rise;
    int     steps;
    float   going;
    float   waist;
    float   land_lo, land_hi;
    float   uv;
} ref_stair_params;

static cerr ref_stair_emit(struct mesh *m, const ref_stair_params *p)
{
    float rs = p->rise / p->steps, hw = p->width * 0.5f;
    float lt = p->waist + 0.06f;

    if (p->land_lo > 0.0f)  CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, -p->land_lo, 0.0f, -lt, 0.0f, -hw, hw, p->uv));
    if (p->land_hi > 0.0f) {
        float top = p->steps * p->going;

        CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, top, top + p->land_hi, p->rise - lt, p->rise, -hw, hw, p->uv));
    }

    for (int k = 0; k < p->steps; k++)
        CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, k * p->going, (k + 1) * p->going, k * rs - p->waist,
                                  (k + 1) * rs, -hw, hw, p->uv));

    return CERR_OK;
}

/*
 * The reference building's three flights against a reference copy of the
 * flight emitter they replace, called the way the reference building calls
 * it: 5 m wide, centered 0.03 + 2.5 in from the chord of each flight's
 * window and halfway along it, climbing a 6 m storey in 50 risers of 0.12
 * and 0.20 goings on a 0.22 waist, without landings; and once more with
 * landings at either end, 0.28 thick, the waist and 0.06. A riser_max of
 * 0.12 takes the 50 risers the original was given; so would anything up
 * to just under 6 / 49.
 */
static int stair_parity_test(void)
{
    const int n = REF_WELL_SIDES;
    const float H = REF_FLOOR_H, sw = REF_STAIR_W, waist = 0.22f;
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    pcg_stair_params p;
    pcg_stair_params_init(&p);
    p.width = sw;
    p.riser_max = 0.12f;
    p.going = REF_GOING;
    p.waist = waist;
    p.landing_t = waist + 0.06f;
    if (CRES_RET(pcg_stair_steps(&p, H), return EXIT_FAILURE) != REF_STEPS) return EXIT_FAILURE;

    const struct { float lo, hi; } land[] = { { 0.0f, 0.0f }, { 1.5f, 2.0f } };

    for (int fi = 0; fi < 2; fi++)
        for (size_t l = 0; l < array_size(land); l++)
            for (int f = 0; f < REF_FLOORS - 1; f++) {
                const pcg_frame *frame = &frames[fi];
                float y = f * H + REF_DECK_T;
                ref_flight g;

                ref_flight_calc(&g, ref_well_r, n, f, REF_STEPS, REF_GOING);

                ref_stair_params rp = {
                    .org = { g.w.A[0] + g.w.dir[0] * g.land + g.w.in[0] * (0.03f + sw * 0.5f),
                             g.w.A[1] + g.w.dir[1] * g.land + g.w.in[1] * (0.03f + sw * 0.5f) },
                    .dir = { g.w.dir[0], g.w.dir[1] },
                    .width = sw, .rise = H, .steps = REF_STEPS,
                    .going = REF_GOING, .waist = waist,
                    .land_lo = land[l].lo, .land_hi = land[l].hi,
                    .uv = REF_UV_CONCRETE,
                };
                LOCAL_SET(mesh_t, rm) = CRES_RET(ref_new_checked(mesh, .name = "ref flight"), return EXIT_FAILURE);
                LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref flight placed"),
                                                  return EXIT_FAILURE);
                if (failed(ref_stair_emit(rm, &rp)))                            return EXIT_FAILURE;
                if (failed(ref_instance(ref, rm, (vec3){ 0.0f, y, 0.0f })))     return EXIT_FAILURE;

                LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("flight"), return EXIT_FAILURE);
                pcg_ctx ctx = { .sink = sink, .palette = &ref_palette, .frame = *frame };
                const pcg_frame at = { .org = { rp.org[0], y, rp.org[1] }, .dir = { rp.dir[0], rp.dir[1] } };
                CERR_RET(pcg_stair_emit(&ctx, &p, .frame = at, .rise = H, .land_lo = land[l].lo,
                                        .land_hi = land[l].hi),
                         return EXIT_FAILURE);

                struct sink_walk w;
                if (!sink_walk_get(sink, &w) || w.nr != 1)                      return EXIT_FAILURE;
                if (!parity_check("flight", walk_mesh(&w, &concrete), ref, frame, false, REF_UV_CONCRETE))
                    return EXIT_FAILURE;
            }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Wall
 ****************************************************************************/

/* a wall with up to two openings */
struct wall_case {
    pcg_wall_params p;
    size_t          nr_open;
    float           a0;
    float           len;
    float           h;
    pcg_opening     open[2];
};

static cerr wall_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct wall_case *c = priv;

    return pcg_wall_emit(ctx, &c->p, .frame = PCG_FRAME_IDENTITY, .a0 = c->a0, .len = c->len, .h = c->h,
                         .openings = c->nr_open ? c->open : nullptr, .nr_openings = c->nr_open);
}

/* the wall's volume: its whole slab less what the openings take out */
static double wall_volume(const struct wall_case *c)
{
    double area = (double)c->len * c->h;

    for (size_t i = 0; i < c->nr_open; i++) {
        const pcg_opening *o = &c->open[i];
        float top = o->h > 0.0f && o->h < c->h ? o->h : c->h;

        area -= (double)o->w * top;
    }

    return area * c->p.t;
}

/* @c emits @nr_boxes boxes over the local box [@lo, @hi] into one bucket, as a closed solid of its volume */
static bool wall_case_ok(const struct wall_case *c, size_t nr_boxes, const vec3 lo, const vec3 hi)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(wall_case_emit, c, &ref_palette, &PCG_FRAME_IDENTITY),
                                         return false);
    struct sink_walk w;
    double vol = wall_volume(c);

    if (!sink_walk_get(sink, &w) || w.nr != 1 || walk_nr_vx(&w) != nr_boxes * 36)       return false;
    if (!walk_aabb_is(&w, &PCG_FRAME_IDENTITY, lo, hi))                                 return false;
    if (!walk_closed(&w) || fabs(walk_volume(&w) - vol) > 1e-4 * vol)                   return false;

    return element_frames_ok(wall_case_emit, c, &ref_palette, true);
}

static int wall_test(void)
{
    struct wall_case c = { .a0 = 1.5f, .len = 6.0f, .h = 3.0f };
    pcg_wall_params_init(&c.p);

    for (int s = 0; s < PCG_WALL_NR_SURF; s++)
        if (c.p.surf[s].role != PCG_ROLE_CLADDING || c.p.surf[s].collide != PCG_COLLIDE_SOLID || c.p.surf[s].skip)
            return EXIT_FAILURE;

    /* a plain wall from a0, its -c face on the line */
    const vec3 lo = { c.a0, 0.0f, 0.0f }, hi = { c.a0 + c.len, c.h, c.p.t };
    if (!wall_case_ok(&c, 1, lo, hi))                                           return EXIT_FAILURE;

    /* a door with a header over it, and one whose head is the top: 3 pieces and a header */
    struct wall_case doors = c;
    doors.open[0] = (pcg_opening){ .at = 1.5f, .w = 1.0f, .h = 2.1f };
    doors.open[1] = (pcg_opening){ .at = 4.5f, .w = 1.2f, .h = 3.0f };
    doors.nr_open = 2;
    if (!wall_case_ok(&doors, 4, lo, hi))                                       return EXIT_FAILURE;

    /* openings flush with both ends leave no empty pieces; a head at 0 is open all the way up */
    struct wall_case flush = c;
    flush.open[0] = (pcg_opening){ .at = 0.5f, .w = 1.0f };
    flush.open[1] = (pcg_opening){ .at = 5.5f, .w = 1.0f, .h = 2.0f };
    flush.nr_open = 2;
    if (!wall_case_ok(&flush, 2, (vec3){ c.a0 + 1.0f, 0.0f, 0.0f }, hi))    return EXIT_FAILURE;

    /* zeroed or nonsense style, no length or height, a start that isn't finite, openings that don't fit */
    LOCAL_SET(pcg_sink, bad) = CRES_RET(pcg_sink_new("wall"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bad, .palette = &ref_palette, .frame = PCG_FRAME_IDENTITY };
    struct wall_case z = c;
    z.p = (pcg_wall_params){};
    if (!failed(wall_case_emit(&bc, &z)))       return EXIT_FAILURE;
    z = c;
    z.p.t = NAN;
    if (!failed(wall_case_emit(&bc, &z)))       return EXIT_FAILURE;
    z = c;
    z.p.t = -0.1f;
    if (!failed(wall_case_emit(&bc, &z)))       return EXIT_FAILURE;
    z = c;
    z.len = 0.0f;
    if (!failed(wall_case_emit(&bc, &z)))       return EXIT_FAILURE;
    z = c;
    z.h = -3.0f;
    if (!failed(wall_case_emit(&bc, &z)))       return EXIT_FAILURE;
    z = c;
    z.a0 = INFINITY;
    if (!failed(wall_case_emit(&bc, &z)))       return EXIT_FAILURE;
    /* nothing that isn't finite, not even the head of the last opening, after the pieces before it */
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = { &z.p.t, &z.a0, &z.len, &z.h, &z.open[1].h };

        for (size_t k = 0; k < array_size(fields); k++) {
            z = doors;
            *fields[k] = nonfinite[i];
            if (!failed_inval(wall_case_emit(&bc, &z)))     return EXIT_FAILURE;
        }
    }
    for (size_t i = 0; i < array_size(bad_frames); i++)
        if (!failed_inval(pcg_wall_emit(&bc, &doors.p, .frame = bad_frames[i], .a0 = doors.a0, .len = doors.len,
                                        .h = doors.h, .openings = doors.open, .nr_openings = doors.nr_open)))
            return EXIT_FAILURE;
    z = doors;
    z.open[1].at = 5.8f;
    if (!failed(wall_case_emit(&bc, &z)))       return EXIT_FAILURE;
    z = doors;
    z.open[1].at = 1.9f;
    if (!failed(wall_case_emit(&bc, &z)))       return EXIT_FAILURE;
    z = doors;
    z.open[0].w = 0.0f;
    if (!failed(wall_case_emit(&bc, &z)))       return EXIT_FAILURE;
    if (!failed(pcg_wall_emit(&bc, &c.p, .frame = PCG_FRAME_IDENTITY, .len = c.len, .h = c.h, .nr_openings = 1)))
        return EXIT_FAILURE;
    if (pcg_sink_get_stats(bad).nr_models)      return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int wall_surface_test(void)
{
    struct wall_case c = { .a0 = 1.5f, .len = 6.0f, .h = 3.0f, .open = { { .at = 3.0f, .w = 1.0f, .h = 2.1f } },
                           .nr_open = 1 };
    pcg_wall_params_init(&c.p);
    for (int s = 0; s < PCG_WALL_NR_SURF; s++)  c.p.surf[s].mat = &surf_mat[s];

    /* the two faces on either side, everything else on the reveal, under a yawed and mirrored frame too */
    for (int m = 0; m < 2; m++) {
        pcg_frame f = PCG_FRAME_IDENTITY;
        if (m)  test_frame(&f, 2.2f, true, -3.0f, 4.0f, 7.0f);

        LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(wall_case_emit, &c, nullptr, &f), return EXIT_FAILURE);
        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != PCG_WALL_NR_SURF)   return EXIT_FAILURE;

        struct mesh *face_l = walk_mesh(&w, &surf_mat[PCG_WALL_FACE_L]);
        struct mesh *face_r = walk_mesh(&w, &surf_mat[PCG_WALL_FACE_R]);
        struct mesh *reveal = walk_mesh(&w, &surf_mat[PCG_WALL_REVEAL]);
        const vec3 up = { 0.0f, 1.0f, 0.0f }, down = { 0.0f, -1.0f, 0.0f };
        const vec3 fore = { 1.0f, 0.0f, 0.0f }, back = { -1.0f, 0.0f, 0.0f };
        const vec3 left = { 0.0f, 0.0f, 1.0f }, right = { 0.0f, 0.0f, -1.0f };

        /* two pieces and a header */
        size_t nr = 3 * 6;
        if (!face_l || mesh_nr_vx(face_l) != nr || !mesh_normals_along(face_l, &f, left))           return EXIT_FAILURE;
        if (!face_r || mesh_nr_vx(face_r) != nr || !mesh_normals_along(face_r, &f, right))          return EXIT_FAILURE;
        if (!reveal || mesh_nr_vx(reveal) != 4 * nr)                                                return EXIT_FAILURE;
        if (mesh_count_normals(reveal, &f, up) != nr || mesh_count_normals(reveal, &f, down) != nr) return EXIT_FAILURE;
        if (mesh_count_normals(reveal, &f, fore) != nr || mesh_count_normals(reveal, &f, back) != nr)
            return EXIT_FAILURE;

        /* the -c face on the frame's line, the +c face the thickness off it */
        for (size_t v = 0; v < nr; v++) {
            vec3 pl, pr;

            pcg_frame_point_to_local(&f, &mesh_vx(face_l)[v * 3], pl);
            pcg_frame_point_to_local(&f, &mesh_vx(face_r)[v * 3], pr);
            if (!near(pl[2], c.p.t, 1e-5f) || !near(pr[2], 0.0f, 1e-5f))    return EXIT_FAILURE;
        }

        /* aliased, one bucket of the union */
        struct wall_case a = c;
        for (int s = 0; s < PCG_WALL_NR_SURF; s++)  a.p.surf[s].mat = &panel;
        LOCAL_SET(pcg_sink, as) = CRES_RET(element_sink(wall_case_emit, &a, nullptr, &f), return EXIT_FAILURE);
        struct sink_walk aw;
        struct mesh *sep[PCG_WALL_NR_SURF] = { face_l, face_r, reveal };
        if (!sink_walk_get(as, &aw) || aw.nr != 1 || !meshes_same_tris(aw.mesh, 1, sep, PCG_WALL_NR_SURF))
            return EXIT_FAILURE;

        /* skipped, the +c face is gone */
        a.p.face_l.skip = true;
        LOCAL_SET(pcg_sink, ks) = CRES_RET(element_sink(wall_case_emit, &a, nullptr, &f), return EXIT_FAILURE);
        if (!sink_walk_get(ks, &aw) || aw.nr != 1 || !meshes_same_tris(aw.mesh, 1, &sep[1], PCG_WALL_NR_SURF - 1))
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/* the reference building's measures its rooms depend on */
#define REF_SIDES       6
#define REF_RADIUS      60.0f
#define REF_SLAB_T      0.35f
#define REF_WALL_T      0.22f
#define REF_ENTRANCE    1
#define REF_DOOR_W      1.1f
#define REF_DOOR_H      2.2f
#define REF_ROOM_T      0.09f

/* reference copies of the room sides and the wall mask bits: solid, glass shifted by 4, door by 8 */
enum { REF_ROOM_FRONT, REF_ROOM_BACK, REF_ROOM_LEFT, REF_ROOM_RIGHT };
#define REF_GLASS_WALL_BIT  4
#define REF_DOOR_WALL_BIT   8

typedef struct ref_room {
    float       width;
    float       depth;
    uint32_t    wall_mask;
    bool        width_frac;
} ref_room;

/* a reference copy of the pane frame emitter, members and doorway */
typedef struct ref_frame_params {
    vec2    org, dir;
    float   a0, a1, y0, y1;
    float   fw, ft;
    float   gap_at, gap_w, gap_h;
    float   uv;
} ref_frame_params;

static cerr ref_frame_emit(struct mesh *m, const ref_frame_params *p)
{
    float hft = p->ft * 0.5f;

    CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, p->a0, p->a0 + p->fw, p->y0, p->y1, -hft, hft, p->uv));
    CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, p->a1 - p->fw, p->a1, p->y0, p->y1, -hft, hft, p->uv));
    CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, p->a0 + p->fw, p->a1 - p->fw, p->y1 - p->fw, p->y1, -hft, hft,
                              p->uv));

    if (p->gap_w <= 0.0f)
        return ref_dir_box(m, p->org, p->dir, p->a0 + p->fw, p->a1 - p->fw, p->y0, p->y0 + p->fw, -hft, hft, p->uv);

    float g0 = p->gap_at - p->gap_w * 0.5f, g1 = p->gap_at + p->gap_w * 0.5f;

    CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, p->a0 + p->fw, g0 - p->fw, p->y0, p->y0 + p->fw, -hft, hft, p->uv));
    CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, g1 + p->fw, p->a1 - p->fw, p->y0, p->y0 + p->fw, -hft, hft, p->uv));
    CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, g0 - p->fw, g0, p->y0, p->gap_h, -hft, hft, p->uv));
    CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, g1, g1 + p->fw, p->y0, p->gap_h, -hft, hft, p->uv));
    CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, g0 - p->fw, g1 + p->fw, p->gap_h, p->gap_h + p->fw, -hft, hft,
                              p->uv));

    return CERR_OK;
}

/* reference copies of the frame members' measures and the bronze density that glass borrows */
#define REF_FRAME_W     0.06f
#define REF_FRAME_T     0.05f
#define REF_UV_BRONZE   (1.0f / 1.2f)

/*
 * One room wall as the original strip laid it out: its run frame, run
 * and doorway, and for a glass wall, its pane's origin and length
 */
typedef struct ref_room_wall {
    vec2    org, dir;
    float   r0, r1;
    vec2    pane_org;
    float   pane_len;
    bool    door;
    bool    glass;
} ref_room_wall;

#define REF_ROOM_WALLS_MAX 64

/*
 * One room as the original strip laid it out: its segment's back line
 * and length, and the segment's place in the building, its run along it
 * and front line, the insets the flank walls take out of the front, its
 * wall mask, its ceiling light's center, and the plaque it mounts on a
 * solid or glass front: its center, and its text and room number
 */
typedef struct ref_room_rec {
    vec2        v0, dir;
    float       side_len;
    int         floor, segment, ring, number;
    float       a0, a1, cf;
    float       ins_l, ins_r;
    uint32_t    wall_mask;
    vec2        light;
    vec2        plaque;
    char        text[16];
    bool        front_glass;
    bool        front_door;
    bool        has_plaque;
} ref_room_rec;

#define REF_RING_ROOMS_MAX 8

/* where a room strip's walls go: solid walls, glass panes and their frames, the ceiling lights and the carpets */
typedef struct ref_room_meshes {
    struct mesh *solid;
    struct mesh *glass;
    struct mesh *frames;
    struct mesh *lights;
    struct mesh *carpet;
} ref_room_meshes;

/* reference copies of the ceiling light's measures */
#define REF_LIGHT_H     0.05f
#define REF_LIGHT_R     0.16f
#define REF_LIGHT_SEGS  12
#define REF_LIGHT_DROP  0.10f

/* reference copies of the plaque's measures: proud of a pane or a solid face, past a doorway */
#define REF_PLAQUE_GLASS_OFF    0.05f
#define REF_PLAQUE_SOLID_OFF    0.02f
#define REF_PLAQUE_DOOR_GAP     0.30f
#define REF_PLAQUE_H            0.32f
#define REF_PLAQUE_Y            1.5f

/* reference copies of the carpet's thickness and density: half-meter tiles, 8 to a repeat */
#define REF_CARPET_T    0.012f
#define REF_UV_CARPET   (1.0f / 4.0f)

/*
 * A reference copy of the room strip's wall layout, along one ring
 * segment from @v0 toward @dir: its solid walls, panes, pane frames,
 * carpets and ceiling lights into @m, every wall recorded into @out and
 * every room into @rec, for the elements that replace them
 */
static cerr ref_room_walls(const ref_room_meshes *m, const vec2 v0, const vec2 dir, float side_len, float wall_h,
                           const ref_room *rooms, size_t nr_rooms, ref_room_wall *out, size_t *nr_out,
                           ref_room_rec *rec, size_t *nr_rec)
{
    float rt = REF_ROOM_T;
    vec2 in = { -dir[1], dir[0] };
    float total = 0.0f;

    for (size_t k = 0; k < nr_rooms; k++)   total += rooms[k].width_frac ? rooms[k].width * side_len : rooms[k].width;

    float at = (side_len - total) * 0.5f;
    for (size_t k = 0; k < nr_rooms; k++) {
        const ref_room *room = &rooms[k];
        float w = room->width_frac ? room->width * side_len : room->width;
        float d = room->depth;
        float a0 = at, a1 = at + w;
        float cb = 0.0f, cf = cb + d;

        at = a1;

        uint32_t lmask = 1u << REF_ROOM_LEFT | 1u << (REF_ROOM_LEFT + REF_GLASS_WALL_BIT);
        uint32_t rmask = 1u << REF_ROOM_RIGHT | 1u << (REF_ROOM_RIGHT + REF_GLASS_WALL_BIT);
        float ins_l = room->wall_mask & lmask ? rt : 0.0f;
        float ins_r = room->wall_mask & rmask ? rt : 0.0f;

        for (int side = 0; side < 4; side++) {
            bool is_solid = room->wall_mask & (1u << side);
            bool is_glass = !is_solid && room->wall_mask & (1u << (side + REF_GLASS_WALL_BIT));
            bool doorway = room->wall_mask & (1u << (side + REF_DOOR_WALL_BIT));

            if (!is_solid && !is_glass) continue;

            vec2 rorg, rdir;
            float r0, r1;

            switch (side) {
            case REF_ROOM_FRONT:
            case REF_ROOM_BACK: {
                float c = side == REF_ROOM_FRONT ? cf : cb + rt;

                rorg[0] = v0[0] + in[0] * (c - rt);
                rorg[1] = v0[1] + in[1] * (c - rt);
                rdir[0] = dir[0];
                rdir[1] = dir[1];
                r0 = a0 + ins_l;
                r1 = a1 - ins_r;
                break;
            }
            default: {
                float a = side == REF_ROOM_LEFT ? a0 + rt : a1;

                rorg[0] = v0[0] + dir[0] * a;
                rorg[1] = v0[1] + dir[1] * a;
                rdir[0] = in[0];
                rdir[1] = in[1];
                r0 = cb;
                r1 = cf;
                break;
            }
            }

            if (*nr_out >= REF_ROOM_WALLS_MAX)  return CERR_TOO_LARGE;
            ref_room_wall *rec = &out[(*nr_out)++];
            *rec = (ref_room_wall){
                .org = { rorg[0], rorg[1] }, .dir = { rdir[0], rdir[1] }, .r0 = r0, .r1 = r1,
                .door = doorway, .glass = is_glass,
            };

            if (is_solid) {
                if (doorway) {
                    float mid = (r0 + r1) * 0.5f;
                    float g0 = mid - REF_DOOR_W * 0.5f, g1 = mid + REF_DOOR_W * 0.5f;

                    CERR_RET_CERR(ref_dir_box(m->solid, rorg, rdir, r0, g0, 0.0f, wall_h, 0.0f, rt, REF_UV_PANEL));
                    CERR_RET_CERR(ref_dir_box(m->solid, rorg, rdir, g1, r1, 0.0f, wall_h, 0.0f, rt, REF_UV_PANEL));
                    CERR_RET_CERR(ref_dir_box(m->solid, rorg, rdir, g0, g1, REF_DOOR_H, wall_h, 0.0f, rt,
                                              REF_UV_PANEL));
                } else {
                    CERR_RET_CERR(ref_dir_box(m->solid, rorg, rdir, r0, r1, 0.0f, wall_h, 0.0f, rt, REF_UV_PANEL));
                }
            } else {
                float sx = side < 2 ? in[0] : -dir[0], sz = side < 2 ? in[1] : -dir[1];
                float len = r1 - r0;

                /* the pane spans [0, len] from its origin: bake the run offset in */
                vec2 gorg = { rorg[0] + sx * rt * 0.5f + rdir[0] * r0,
                              rorg[1] + sz * rt * 0.5f + rdir[1] * r0 };

                rec->pane_org[0] = gorg[0];
                rec->pane_org[1] = gorg[1];
                rec->pane_len = len;

                CERR_RET_CERR(ref_glass_emit(m->glass, gorg, rdir, len, 0.0f, wall_h, len * 0.5f,
                                             doorway ? REF_DOOR_W : 0.0f, REF_DOOR_H, REF_UV_BRONZE));
                CERR_RET_CERR(ref_frame_emit(m->frames, &(ref_frame_params){
                    .org = { gorg[0], gorg[1] }, .dir = { rdir[0], rdir[1] },
                    .a0 = 0.0f, .a1 = len, .y0 = 0.0f, .y1 = wall_h,
                    .fw = REF_FRAME_W, .ft = REF_FRAME_T,
                    .gap_at = len * 0.5f, .gap_w = doorway ? REF_DOOR_W : 0.0f,
                    .gap_h = REF_DOOR_H,
                    .uv = REF_UV_BRONZE,
                }));
            }
        }

        /* wall-to-wall carpet: a thin pile plate between the wall inner faces */
        CERR_RET_CERR(ref_dir_box(m->carpet, v0, dir, a0 + rt, a1 - rt, 0.0f, REF_CARPET_T, cb + rt, cf - rt,
                                  REF_UV_CARPET));

        /* one round ceiling fixture per room, flush with the soffit */
        float fa = (a0 + a1) * 0.5f, fc = (cb + cf) * 0.5f;

        if (*nr_rec >= REF_RING_ROOMS_MAX)  return CERR_TOO_LARGE;
        ref_room_rec *rr = &rec[(*nr_rec)++];
        *rr = (ref_room_rec){
            .v0     = { v0[0], v0[1] },
            .dir    = { dir[0], dir[1] },
            .side_len = side_len,
            .a0     = a0,
            .a1     = a1,
            .cf     = cf,
            .ins_l  = ins_l,
            .ins_r  = ins_r,
            .wall_mask = room->wall_mask,
            .light  = { v0[0] + dir[0] * fa + in[0] * fc, v0[1] + dir[1] * fa + in[1] * fc },
        };
        CERR_RET_CERR(_prim_emit_cylinder((vec3){ v0[0] + dir[0] * fa + in[0] * fc, wall_h - REF_LIGHT_DROP,
                                                  v0[1] + dir[1] * fa + in[1] * fc },
                                          REF_LIGHT_H, REF_LIGHT_R, REF_LIGHT_SEGS,
                                          &(prim_emit_opts){ .mesh = m->lights }));

        /* the atrium-facing plaque, on the front wall when the room has one */
        bool fsolid = room->wall_mask & (1u << REF_ROOM_FRONT);
        bool fglass = !fsolid && room->wall_mask & (1u << (REF_ROOM_FRONT + REF_GLASS_WALL_BIT));

        rr->front_glass = fglass;
        rr->front_door = room->wall_mask & (1u << (REF_ROOM_FRONT + REF_DOOR_WALL_BIT));
        rr->has_plaque = fsolid || fglass;
        if (rr->has_plaque) {
            float pc = fglass ? cf - rt * 0.5f + REF_PLAQUE_GLASS_OFF : cf + REF_PLAQUE_SOLID_OFF;
            float pa = (a0 + ins_l + a1 - ins_r) * 0.5f;

            /* beside the doorway rather than over it */
            if (rr->front_door) pa += REF_DOOR_W * 0.5f + REF_PLAQUE_DOOR_GAP;

            rr->plaque[0] = v0[0] + dir[0] * pa + in[0] * pc;
            rr->plaque[1] = v0[1] + dir[1] * pa + in[1] * pc;
        }
    }

    return CERR_OK;
}

/* the reference building's room rings: glass-fronted offices on the outside, glazed both ways inside */
static const ref_room ref_rooms_outer[] = {
    { .width = 0.3f, .width_frac = true, .depth = 4.2f,
      .wall_mask = 1 << REF_ROOM_LEFT | 1 << REF_ROOM_RIGHT | 1 << (REF_ROOM_FRONT + REF_GLASS_WALL_BIT) |
                   1 << (REF_ROOM_FRONT + REF_DOOR_WALL_BIT) },
    { .width = 0.3f, .width_frac = true, .depth = 4.2f,
      .wall_mask = 1 << REF_ROOM_LEFT | 1 << REF_ROOM_RIGHT | 1 << (REF_ROOM_FRONT + REF_GLASS_WALL_BIT) |
                   1 << (REF_ROOM_FRONT + REF_DOOR_WALL_BIT) },
};

static const ref_room ref_rooms_inner[] = {
    { .width = 0.2f, .width_frac = true, .depth = 3.6f,
      .wall_mask = 1 << REF_ROOM_LEFT | 1 << REF_ROOM_RIGHT | 1 << (REF_ROOM_FRONT + REF_GLASS_WALL_BIT) |
                   1 << (REF_ROOM_BACK + REF_GLASS_WALL_BIT) },
    { .width = 0.2f, .width_frac = true, .depth = 3.6f,
      .wall_mask = 1 << REF_ROOM_LEFT | 1 << REF_ROOM_RIGHT | 1 << (REF_ROOM_FRONT + REF_GLASS_WALL_BIT) |
                   1 << (REF_ROOM_BACK + REF_GLASS_WALL_BIT) },
    { .width = 0.2f, .width_frac = true, .depth = 3.6f,
      .wall_mask = 1 << REF_ROOM_LEFT | 1 << REF_ROOM_RIGHT | 1 << (REF_ROOM_FRONT + REF_GLASS_WALL_BIT) |
                   1 << (REF_ROOM_BACK + REF_GLASS_WALL_BIT) },
};

/* the inner rooms' carousel: a different side dropped per segment and floor */
static void ref_room_carousel(int floor, int segment, ref_room *room)
{
    uint32_t side = (uint32_t)(segment + floor) & 3u;

    room->wall_mask &= ~(1u << side | 1u << (side + REF_GLASS_WALL_BIT));
}

/* the reference building's wall height: a storey less the slab and the deck, 0.05 into the slab above */
#define REF_WALL_H (REF_FLOOR_H - REF_SLAB_T - REF_DECK_T + 0.05f)

/*
 * The walls of one storey's rooms on ring @ring of segment @segment, as
 * the reference building lays them out: the ring's back polygon is the
 * perimeter wall's inner face moved in by the ring's inset. @variant 1
 * turns every room's walls solid, with doorways front and back, through
 * the same layout.
 */
static cerr ref_ring_walls(const ref_room_meshes *m, int floor, int segment, int ring, int variant,
                           ref_room_wall *out, size_t *nr_out, ref_room_rec *rec, size_t *nr_rec)
{
    const ref_room *rooms = ring ? ref_rooms_inner : ref_rooms_outer;
    size_t nr_rooms = ring ? array_size(ref_rooms_inner) : array_size(ref_rooms_outer);
    float apothem = REF_RADIUS * cosf((float)M_PI / REF_SIDES);
    float rring = REF_RADIUS * (apothem - REF_WALL_T - (ring ? 6.5f : 0.0f)) / apothem;
    ref_room resolved[4];
    vec2 v0, v1, dir;

    for (size_t k = 0; k < nr_rooms; k++) {
        resolved[k] = rooms[k];
        if (ring)       ref_room_carousel(floor, segment, &resolved[k]);
        if (variant)    resolved[k].wall_mask = 0xf | 1 << (REF_ROOM_FRONT + REF_DOOR_WALL_BIT) |
                                                1 << (REF_ROOM_BACK + REF_DOOR_WALL_BIT);
    }

    ref_ngon_vertex(segment, REF_SIDES, rring, v0);
    ref_ngon_vertex(segment + 1, REF_SIDES, rring, v1);
    dir[0] = v1[0] - v0[0];
    dir[1] = v1[1] - v0[1];

    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1]);

    dir[0] /= len;
    dir[1] /= len;

    size_t first = *nr_rec;
    CERR_RET_CERR(ref_room_walls(m, v0, dir, len, REF_WALL_H, resolved, nr_rooms, out, nr_out, rec, nr_rec));
    for (size_t k = first; k < *nr_rec; k++) {
        rec[k].floor = floor;
        rec[k].segment = segment;
        rec[k].ring = ring;
    }

    return CERR_OK;
}

/*
 * Called with the walls and the rooms of one ring segment of one storey,
 * @y its deck, in their own meshes at the building's origin
 */
typedef cerr (*ref_ring_fn)(const ref_room_wall *rw, size_t nr, const ref_room_rec *rooms, size_t nr_rooms,
                            const ref_room_meshes *m, float y, void *priv);

/* every ring segment of every storey of the reference building, but the ground floor's entrance */
static cerr ref_rooms_for_each(int variant, ref_ring_fn fn, void *priv)
{
    for (int f = 0; f < REF_FLOORS; f++) {
        float y = f * REF_FLOOR_H + REF_DECK_T;
        /* rooms are numbered across a storey's rings and segments, "302" style */
        int room_no = 0;

        for (int ring = 0; ring < 2; ring++)
            for (int i = 0; i < REF_SIDES; i++) {
                ref_room_wall rw[REF_ROOM_WALLS_MAX];
                ref_room_rec rooms[REF_RING_ROOMS_MAX];
                size_t nr = 0, nr_rooms = 0;

                if (!f && i == REF_ENTRANCE)    continue;

                LOCAL_SET(mesh_t, solid) = CRES_RET_CERR(ref_new_checked(mesh, .name = "ref solid"));
                LOCAL_SET(mesh_t, glass) = CRES_RET_CERR(ref_new_checked(mesh, .name = "ref glass"));
                LOCAL_SET(mesh_t, frames) = CRES_RET_CERR(ref_new_checked(mesh, .name = "ref frames"));
                LOCAL_SET(mesh_t, lights) = CRES_RET_CERR(ref_new_checked(mesh, .name = "ref lights"));
                LOCAL_SET(mesh_t, carpet) = CRES_RET_CERR(ref_new_checked(mesh, .name = "ref carpet"));
                const ref_room_meshes m = {
                    .solid = solid, .glass = glass, .frames = frames, .lights = lights, .carpet = carpet,
                };

                /* the light emitter keeps the tangents of a mesh that has them */
                CRES_RET_CERR(_prim_begin(REF_LIGHT_SEGS * 12, &(prim_emit_opts){ .mesh = lights, .tangents = true }));
                CERR_RET_CERR(ref_ring_walls(&m, f, i, ring, variant, rw, &nr, rooms, &nr_rooms));
                for (size_t k = 0; k < nr_rooms; k++)
                    snprintf(rooms[k].text, sizeof(rooms[k].text), "%d%02d", f, rooms[k].number = ++room_no);
                CERR_RET_CERR(fn(rw, nr, rooms, nr_rooms, &m, y, priv));
            }
    }

    return CERR_OK;
}

/* what the walls emit into, and the reference mesh they are compared against */
struct wall_parity {
    pcg_ctx             ctx;
    struct mesh         *ref;
    pcg_wall_params     wall;
};

static cerr wall_parity_ring(const ref_room_wall *rw, size_t nr, const ref_room_rec *rooms, size_t nr_rooms,
                             const ref_room_meshes *m, float y, void *priv)
{
    struct wall_parity *rp = priv;

    CERR_RET_CERR(ref_instance(rp->ref, m->solid, (vec3){ 0.0f, y, 0.0f }));

    for (size_t k = 0; k < nr; k++) {
        if (rw[k].glass)    continue;

        float len = rw[k].r1 - rw[k].r0;
        const pcg_opening door = { .at = len * 0.5f, .w = REF_DOOR_W, .h = REF_DOOR_H };
        const pcg_frame at = { .org = { rw[k].org[0], y, rw[k].org[1] }, .dir = { rw[k].dir[0], rw[k].dir[1] } };

        CERR_RET_CERR(pcg_wall_emit(&rp->ctx, &rp->wall, .frame = at, .a0 = rw[k].r0, .len = len, .h = REF_WALL_H,
                                    .openings = &door, .nr_openings = rw[k].door));
    }

    return CERR_OK;
}

/*
 * The reference building's room walls against a reference copy of the
 * strip layout that emits them inline: 0.09 thick and 5.65 tall on each
 * storey's deck, from the two-room outer ring and the three-room inner one
 * with its carousel, on every segment but the ground floor's entrance;
 * and once more with every wall solid and doored front and back, 1.1 by
 * 2.2, through the same layout. Each wall takes its run frame and run
 * from the layout, its doorway centered on it.
 */
static int wall_parity_test(void)
{
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    for (int fi = 0; fi < 2; fi++)
        for (int variant = 0; variant < 2; variant++) {
            LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("wall"), return EXIT_FAILURE);
            LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref walls"), return EXIT_FAILURE);
            struct wall_parity rp = {
                .ctx = { .sink = sink, .palette = &ref_palette, .frame = frames[fi] },
                .ref = ref,
            };
            pcg_wall_params_init(&rp.wall);
            rp.wall.t = REF_ROOM_T;

            CERR_RET(ref_rooms_for_each(variant, wall_parity_ring, &rp), return EXIT_FAILURE);

            struct sink_walk w;
            if (!sink_walk_get(sink, &w) || w.nr != 1)                                  return EXIT_FAILURE;
            if (!parity_check("room walls", walk_mesh(&w, &panel), ref, &frames[fi], false, REF_UV_PANEL))
                return EXIT_FAILURE;
        }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Glazing
 ****************************************************************************/

/* the reference layout's glazing finishes: blended glass in bronze members */
static const pcg_material glass = {
    .name = "glass", .uv_mode = PCG_UV_METERS, .alpha_blend = true, .skip_shadow = true,
};
static const pcg_palette glazing_palette = {
    .mat[PCG_ROLE_CLADDING] = &panel,
    .mat[PCG_ROLE_GLASS]    = &glass,
    .mat[PCG_ROLE_METAL]    = &bronze,
};

/* a framed pane with up to two openings */
struct glazing_case {
    pcg_glazing_params  p;
    size_t              nr_open;
    float               len;
    float               y0;
    float               y1;
    pcg_opening         open[2];
};

static cerr glazing_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct glazing_case *c = priv;

    return pcg_glazing_emit(ctx, &c->p, .frame = PCG_FRAME_IDENTITY, .len = c->len, .y0 = c->y0, .y1 = c->y1,
                            .openings = c->nr_open ? c->open : nullptr, .nr_openings = c->nr_open);
}

/*
 * @c emits @nr_pieces pane pieces and members of @nr_boxes boxes with a
 * face area of @area in the pane's plane, as a closed surface
 */
static bool glazing_case_ok(const struct glazing_case *c, size_t nr_pieces, size_t nr_boxes, double area)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(glazing_case_emit, c, &glazing_palette, &PCG_FRAME_IDENTITY),
                                         return false);
    struct sink_walk w;
    float ht = c->p.member_t * 0.5f;
    double vol = area * c->p.member_t;

    if (!sink_walk_get(sink, &w) || w.nr != 2)                                          return false;

    struct mesh *pane = walk_mesh(&w, &glass), *member = walk_mesh(&w, &bronze);
    if (!pane || mesh_nr_vx(pane) != nr_pieces * 12 || !mesh_tangents_ok(pane))         return false;
    if (!member || mesh_nr_vx(member) != nr_boxes * 36)                                 return false;
    if (w.key[0].collide == w.key[1].collide)                                           return false;
    if (!walk_aabb_is(&w, &PCG_FRAME_IDENTITY, (vec3){ 0.0f, c->y0, -ht }, (vec3){ c->len, c->y1, ht }))
        return false;
    if (!walk_closed(&w) || fabs(walk_volume(&w) - vol) > 1e-4 * vol)                   return false;

    return element_frames_ok(glazing_case_emit, c, &glazing_palette, true);
}

static int glazing_test(void)
{
    struct glazing_case c = { .len = 4.0f, .y1 = 2.8f };
    pcg_glazing_params_init(&c.p);

    if (c.p.pane.role != PCG_ROLE_GLASS || c.p.pane.collide != PCG_COLLIDE_SOLID || c.p.pane.skip)  return EXIT_FAILURE;
    if (c.p.member.role != PCG_ROLE_METAL || c.p.member.collide != PCG_COLLIDE_NONE || c.p.member.skip)
        return EXIT_FAILURE;
    if (!c.p.sill)  return EXIT_FAILURE;

    double fw = c.p.member_w, h = c.y1 - c.y0, rail = (c.len - 2.0 * fw) * fw;

    /* one pane in two verticals, a top member and a sill */
    if (!glazing_case_ok(&c, 1, 4, 2.0 * fw * h + 2.0 * rail))  return EXIT_FAILURE;

    /* without the sill */
    struct glazing_case bare = c;
    bare.p.sill = false;
    if (!glazing_case_ok(&bare, 1, 3, 2.0 * fw * h + rail))                     return EXIT_FAILURE;

    /* raised, as a window band */
    struct glazing_case band = c;
    band.y0 = 0.9f;
    band.y1 = 2.4f;
    double bh = band.y1 - band.y0;
    if (!glazing_case_ok(&band, 1, 4, 2.0 * fw * bh + 2.0 * rail))  return EXIT_FAILURE;

    /* a door under a transom: the sill splits, jambs rise to a head member */
    struct glazing_case door = c;
    door.open[0] = (pcg_opening){ .at = 2.0f, .w = 1.0f, .h = 2.1f };
    door.nr_open = 1;
    double dw = door.open[0].w, dh = door.open[0].h;
    double sill = rail - (dw + 2.0 * fw) * fw;
    if (!glazing_case_ok(&door, 3, 8, 2.0 * fw * h + rail + sill + 2.0 * fw * dh + (dw + 2.0 * fw) * fw))
        return EXIT_FAILURE;

    /* an opening without a head splits the top member too, its jambs the full height */
    struct glazing_case open = door;
    open.open[0].h = 0.0f;
    if (!glazing_case_ok(&open, 2, 8, 2.0 * fw * h + 2.0 * sill + 2.0 * fw * h))    return EXIT_FAILURE;

    /* frameless: the pane alone, an opening up to its end */
    struct glazing_case frameless = c;
    frameless.p.member.skip = true;
    frameless.open[0] = (pcg_opening){ .at = 3.5f, .w = 1.0f };
    frameless.nr_open = 1;
    LOCAL_SET(pcg_sink, fs) = CRES_RET(element_sink(glazing_case_emit, &frameless, &glazing_palette,
                                                    &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(fs, &w) || w.nr != 1 || walk_nr_vx(&w) != 12)                return EXIT_FAILURE;
    if (!element_frames_ok(glazing_case_emit, &frameless, &glazing_palette, false)) return EXIT_FAILURE;

    /* zeroed or nonsense style, a pane without length or height, openings or members that don't fit */
    LOCAL_SET(pcg_sink, bad) = CRES_RET(pcg_sink_new("glazing"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bad, .palette = &glazing_palette, .frame = PCG_FRAME_IDENTITY };
    struct glazing_case z = c;
    z.p = (pcg_glazing_params){};
    if (!failed(glazing_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.p.member_w = NAN;
    if (!failed(glazing_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.p.member_t = 0.0f;
    if (!failed(glazing_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.p.min_seg = -0.01f;
    if (!failed(glazing_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.len = 0.0f;
    if (!failed(glazing_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.y1 = z.y0;
    if (!failed(glazing_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.len = 0.1f;
    if (!failed(glazing_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.y1 = 0.1f;
    if (!failed(glazing_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = door;
    z.open[0].at = 3.8f;
    if (!failed(glazing_case_emit(&bc, &z)))    return EXIT_FAILURE;
    /* no room for a jamb at the end, nor between two openings, nor for a head under the top */
    z = frameless;
    z.p.member.skip = false;
    if (!failed(glazing_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = door;
    z.open[0] = (pcg_opening){ .at = 1.0f, .w = 1.0f, .h = 2.1f };
    z.open[1] = (pcg_opening){ .at = 2.05f, .w = 1.0f, .h = 2.1f };
    z.nr_open = 2;
    if (!failed(glazing_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = door;
    z.open[0].h = 2.7f;
    if (!failed(glazing_case_emit(&bc, &z)))    return EXIT_FAILURE;
    /* nothing that isn't finite, framed or not */
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = { &z.p.member_w, &z.p.member_t, &z.p.min_seg, &z.len, &z.y0, &z.y1, &z.open[0].h };

        for (size_t k = 0; k < array_size(fields); k++) {
            z = door;
            *fields[k] = nonfinite[i];
            if (!failed_inval(glazing_case_emit(&bc, &z)))  return EXIT_FAILURE;
            z = door;
            z.p.member.skip = true;
            *fields[k] = nonfinite[i];
            if (!failed_inval(glazing_case_emit(&bc, &z)))  return EXIT_FAILURE;
        }
    }
    for (size_t i = 0; i < array_size(bad_frames); i++)
        if (!failed_inval(pcg_glazing_emit(&bc, &door.p, .frame = bad_frames[i], .len = door.len, .y0 = door.y0,
                                           .y1 = door.y1, .openings = door.open, .nr_openings = door.nr_open)))
            return EXIT_FAILURE;
    if (pcg_sink_get_stats(bad).nr_models)      return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int glazing_surface_test(void)
{
    struct glazing_case c = { .len = 4.0f, .y1 = 2.8f, .open = { { .at = 2.0f, .w = 1.0f, .h = 2.1f } },
                              .nr_open = 1 };
    pcg_glazing_params_init(&c.p);
    for (int s = 0; s < PCG_GLAZING_NR_SURF; s++)   c.p.surf[s].mat = &surf_mat[s];

    /* the pane both ways in the frame's plane, the members around it, under a yawed and mirrored frame too */
    for (int m = 0; m < 2; m++) {
        pcg_frame f = PCG_FRAME_IDENTITY;
        if (m)  test_frame(&f, 2.2f, true, -3.0f, 4.0f, 7.0f);

        LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(glazing_case_emit, &c, nullptr, &f), return EXIT_FAILURE);
        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != PCG_GLAZING_NR_SURF)    return EXIT_FAILURE;

        struct mesh *pane = walk_mesh(&w, &surf_mat[PCG_GLAZING_PANE]);
        struct mesh *member = walk_mesh(&w, &surf_mat[PCG_GLAZING_MEMBER]);
        const vec3 left = { 0.0f, 0.0f, 1.0f }, right = { 0.0f, 0.0f, -1.0f };

        if (!pane || mesh_nr_vx(pane) != 3 * 12 || !member || mesh_nr_vx(member) != 8 * 36) return EXIT_FAILURE;
        if (mesh_count_normals(pane, &f, left) != 3 * 6 || mesh_count_normals(pane, &f, right) != 3 * 6)
            return EXIT_FAILURE;
        for (size_t v = 0; v < mesh_nr_vx(pane); v++) {
            vec3 p;

            pcg_frame_point_to_local(&f, &mesh_vx(pane)[v * 3], p);
            if (!near(p[2], 0.0f, 1e-5f))                                               return EXIT_FAILURE;
        }

        /* aliased into one bucket, the union */
        struct glazing_case a = c;
        for (int s = 0; s < PCG_GLAZING_NR_SURF; s++) {
            a.p.surf[s].mat = &bronze;
            a.p.surf[s].collide = PCG_COLLIDE_SOLID;
        }
        LOCAL_SET(pcg_sink, as) = CRES_RET(element_sink(glazing_case_emit, &a, nullptr, &f), return EXIT_FAILURE);
        struct sink_walk aw;
        struct mesh *sep[PCG_GLAZING_NR_SURF] = { pane, member };
        if (!sink_walk_get(as, &aw) || aw.nr != 1 || !meshes_same_tris(aw.mesh, 1, sep, PCG_GLAZING_NR_SURF))
            return EXIT_FAILURE;

        /* skipped, the pane is gone */
        a.p.pane.skip = true;
        LOCAL_SET(pcg_sink, ks) = CRES_RET(element_sink(glazing_case_emit, &a, nullptr, &f), return EXIT_FAILURE);
        if (!sink_walk_get(ks, &aw) || aw.nr != 1 || !meshes_same_tris(aw.mesh, 1, &sep[1], 1)) return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/* what the glazing emits into, and the reference meshes it is compared against */
struct glazing_parity {
    pcg_ctx             ctx;
    struct mesh         *ref_glass;
    struct mesh         *ref_frames;
    pcg_glazing_params  glazing;
};

static cerr glazing_parity_ring(const ref_room_wall *rw, size_t nr, const ref_room_rec *rooms, size_t nr_rooms,
                                const ref_room_meshes *m, float y, void *priv)
{
    struct glazing_parity *gp = priv;

    CERR_RET_CERR(ref_instance(gp->ref_glass, m->glass, (vec3){ 0.0f, y, 0.0f }));
    CERR_RET_CERR(ref_instance(gp->ref_frames, m->frames, (vec3){ 0.0f, y, 0.0f }));

    for (size_t k = 0; k < nr; k++) {
        if (!rw[k].glass)   continue;

        float len = rw[k].pane_len;
        const pcg_opening door = { .at = len * 0.5f, .w = REF_DOOR_W, .h = REF_DOOR_H };
        const pcg_frame at = { .org = { rw[k].pane_org[0], y, rw[k].pane_org[1] },
                               .dir = { rw[k].dir[0], rw[k].dir[1] } };

        CERR_RET_CERR(pcg_glazing_emit(&gp->ctx, &gp->glazing, .frame = at, .len = len, .y0 = 0.0f,
                                       .y1 = REF_WALL_H, .openings = &door, .nr_openings = rw[k].door));
    }

    return CERR_OK;
}

/*
 * The reference building's glass room walls against reference copies of
 * the pane and pane frame emitters, laid out by the reference copy of the
 * room strip: the outer ring's glass fronts with their 1.1 by 2.2
 * doorways, the inner ring's glass fronts and backs without, all 5.65
 * tall on each storey's deck in 0.06 by 0.05 members, the pane centered
 * in the room wall's thickness.
 */
static int glazing_parity_test(void)
{
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    for (int fi = 0; fi < 2; fi++) {
        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("glazing"), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, ref_glass) = CRES_RET(ref_new_checked(mesh, .name = "ref glass"), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, ref_frames) = CRES_RET(ref_new_checked(mesh, .name = "ref frames"), return EXIT_FAILURE);
        struct glazing_parity gp = {
            .ctx        = { .sink = sink, .palette = &glazing_palette, .frame = frames[fi] },
            .ref_glass  = ref_glass,
            .ref_frames = ref_frames,
        };
        pcg_glazing_params_init(&gp.glazing);

        CERR_RET(ref_rooms_for_each(0, glazing_parity_ring, &gp), return EXIT_FAILURE);

        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != 2)                                      return EXIT_FAILURE;
        if (!parity_check("room glass", walk_mesh(&w, &glass), ref_glass, &frames[fi], false, REF_UV_BRONZE))
            return EXIT_FAILURE;
        if (!parity_check("room frames", walk_mesh(&w, &bronze), ref_frames, &frames[fi], false, REF_UV_BRONZE))
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Window wall
 ****************************************************************************/

/* a window wall with up to two portals */
struct window_wall_case {
    pcg_window_wall_params  p;
    float                   len;
    float                   h;
    pcg_opening             open[2];
    size_t                  nr_open;
};

static cerr window_wall_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct window_wall_case *c = priv;

    return pcg_window_wall_emit(ctx, &c->p, .frame = PCG_FRAME_IDENTITY, .len = c->len, .h = c->h,
                                .openings = c->nr_open ? c->open : nullptr, .nr_openings = c->nr_open);
}

/*
 * @c emits @nr_solid boxes of cladding, @nr_mullions of metal and
 * @nr_panes pieces of glass, with the solids' face area in the wall's
 * plane @area, as a closed surface over the wall's whole extent
 */
static bool window_wall_case_ok(const struct window_wall_case *c, size_t nr_solid, size_t nr_mullions,
                                size_t nr_panes, double area)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(window_wall_case_emit, c, &glazing_palette,
                                                      &PCG_FRAME_IDENTITY),
                                         return false);
    struct sink_walk w;
    float ht = c->p.t * 0.5f;
    double vol = area * c->p.t;

    if (!sink_walk_get(sink, &w) || w.nr != (nr_mullions ? 3 : 2))                      return false;

    struct mesh *solid = walk_mesh(&w, &panel), *mullion = walk_mesh(&w, &bronze), *pane = walk_mesh(&w, &glass);
    if (!solid || mesh_nr_vx(solid) != nr_solid * 36)                                                   return false;
    if (nr_mullions && (!mullion || mesh_nr_vx(mullion) != nr_mullions * 36))                           return false;
    if (!pane || mesh_nr_vx(pane) != nr_panes * 12)                                                     return false;
    if (!walk_aabb_is(&w, &PCG_FRAME_IDENTITY, (vec3){ 0.0f, 0.0f, -ht }, (vec3){ c->len, c->h, ht }))  return false;
    if (!walk_closed(&w) || fabs(walk_volume(&w) - vol) > 1e-4 * vol)                                   return false;

    /* the band's glass between the sill, or the floor, and the head */
    float lo = c->p.open_below ? 0.0f : c->p.sill;
    if (!mesh_y_within(pane, &PCG_FRAME_IDENTITY, lo, c->p.head))                       return false;
    if (mullion && !mesh_y_within(mullion, &PCG_FRAME_IDENTITY, lo, c->p.head))         return false;

    return element_frames_ok(window_wall_case_emit, c, &glazing_palette, true);
}

static int window_wall_test(void)
{
    struct window_wall_case c = { .len = 10.0f, .h = 4.0f };
    pcg_window_wall_params_init(&c.p);

    for (int s = 0; s < PCG_WINDOW_WALL_NR_SURF; s++) {
        pcg_role role = s == PCG_WINDOW_WALL_MULLION ? PCG_ROLE_METAL : PCG_ROLE_CLADDING;

        if (c.p.surf[s].role != role || c.p.surf[s].collide != PCG_COLLIDE_SOLID || c.p.surf[s].skip)
            return EXIT_FAILURE;
    }
    if (c.p.open_below || !c.p.glazing.member.skip || c.p.glazing.pane.skip)    return EXIT_FAILURE;
    if (c.p.glazing.pane.role != PCG_ROLE_GLASS)                                return EXIT_FAILURE;

    /*
     * 10 m in 2 bays of 4 panes: a spandrel, a header, two end posts and
     * a bay post, 6 mullions and one pane
     */
    double t = c.p.t, pw = c.p.post_w, mw = c.p.mull_w, band = c.p.head - c.p.sill;
    double sp = c.len * c.p.sill, hd = c.len * (c.h - c.p.head);
    if (!window_wall_case_ok(&c, 5, 6, 1, sp + hd + 3.0 * pw * band + 6.0 * mw * band)) return EXIT_FAILURE;

    /*
     * an entrance in the middle: the spandrel splits, posts either side
     * rise from the floor, the bay post and the two mullions in its way go,
     * and so does its glass
     */
    struct window_wall_case door = c;
    door.open[0] = (pcg_opening){ .at = 5.0f, .w = 2.6f, .h = c.p.head };
    door.nr_open = 1;
    double dw = door.open[0].w;
    if (!window_wall_case_ok(&door, 7, 4, 2, sp - dw * c.p.sill + hd + 2.0 * pw * band + 2.0 * pw * c.p.head +
                                              4.0 * mw * band))
        return EXIT_FAILURE;

    /* a narrower entrance: the mullions that would touch its posts go too */
    struct window_wall_case narrow = door;
    narrow.open[0].w = 2.0f;
    dw = narrow.open[0].w;
    if (!window_wall_case_ok(&narrow, 7, 4, 2, sp - dw * c.p.sill + hd + 2.0 * pw * band + 2.0 * pw * c.p.head +
                                                4.0 * mw * band))
        return EXIT_FAILURE;

    /* an entrance off the middle: the bay post that would touch its post goes, the mullions clear of it stay */
    struct window_wall_case off = door;
    off.open[0] = (pcg_opening){ .at = 6.2f, .w = 1.6f, .h = c.p.head };
    dw = off.open[0].w;
    if (!window_wall_case_ok(&off, 7, 5, 2, sp - dw * c.p.sill + hd + 2.0 * pw * band + 2.0 * pw * c.p.head +
                                            5.0 * mw * band))
        return EXIT_FAILURE;

    /* a lower head leaves a transom pane over the entrance */
    struct window_wall_case transom = door;
    transom.open[0].h = 2.4f;
    LOCAL_SET(pcg_sink, ts) = CRES_RET(element_sink(window_wall_case_emit, &transom, &glazing_palette,
                                                    &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(ts, &w) || w.nr != 3 || mesh_nr_vx(walk_mesh(&w, &glass)) != 3 * 12) return EXIT_FAILURE;

    /* a storefront: no spandrel, posts, mullions and glass down to the floor */
    struct window_wall_case store = c;
    store.p.open_below = true;
    if (!window_wall_case_ok(&store, 4, 6, 1, hd + 3.0 * pw * c.p.head + 6.0 * mw * c.p.head))  return EXIT_FAILURE;

    /* a short wall is one bay */
    struct window_wall_case one = c;
    one.len = 1.0f;
    if (!window_wall_case_ok(&one, 4, 0, 1, one.len * c.p.sill + one.len * (c.h - c.p.head) + 2.0 * pw * band))
        return EXIT_FAILURE;

    /*
     * zeroed or nonsense style, a site that doesn't fit, openings without
     * room for their posts or with a head outside the band, bays or panes
     * no wider than their posts and mullions
     */
    LOCAL_SET(pcg_sink, bad) = CRES_RET(pcg_sink_new("window wall"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bad, .palette = &glazing_palette, .frame = PCG_FRAME_IDENTITY };
    struct window_wall_case z = c;
    z.p = (pcg_window_wall_params){};
    if (!failed(window_wall_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.p.t = NAN;
    if (!failed(window_wall_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.p.mull_pitch = 0.0f;
    if (!failed(window_wall_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.p.head = z.p.sill;
    if (!failed(window_wall_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.h = z.p.head;
    if (!failed(window_wall_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.len = 0.5f;
    if (!failed(window_wall_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.p.glazing.min_seg = -1.0f;
    if (!failed(window_wall_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = door;
    z.open[0].at = 9.0f;
    if (!failed(window_wall_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = door;
    z.open[0].at = 1.6f;
    if (!failed(window_wall_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = door;
    z.open[0] = (pcg_opening){ .at = 3.0f, .w = 1.0f, .h = c.p.head };
    z.open[1] = (pcg_opening){ .at = 4.0f, .w = 0.4f, .h = c.p.head };
    z.nr_open = 2;
    if (!failed(window_wall_case_emit(&bc, &z)))    return EXIT_FAILURE;
    const float bad_head[] = { 0.0f, c.p.sill, 3.5f, c.h, NAN };
    for (size_t i = 0; i < array_size(bad_head); i++) {
        z = door;
        z.open[0].h = bad_head[i];
        if (!failed(window_wall_case_emit(&bc, &z)))    return EXIT_FAILURE;
    }
    z = c;
    z.len = 1.0f;
    z.p.bay = 0.2f;
    if (!failed(window_wall_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.p.mull_pitch = 0.05f;
    if (!failed(window_wall_case_emit(&bc, &z)))    return EXIT_FAILURE;
    /* nothing that isn't finite, the glass's style included */
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = {
            &z.p.t, &z.p.sill, &z.p.head, &z.p.bay, &z.p.post_w, &z.p.mull_w, &z.p.mull_pitch, &z.len, &z.h,
            &z.p.glazing.member_w, &z.p.glazing.member_t, &z.p.glazing.min_seg, &z.open[0].h,
        };

        for (size_t k = 0; k < array_size(fields); k++) {
            z = door;
            *fields[k] = nonfinite[i];
            if (!failed_inval(window_wall_case_emit(&bc, &z)))  return EXIT_FAILURE;
        }
    }
    for (size_t i = 0; i < array_size(bad_frames); i++)
        if (!failed_inval(pcg_window_wall_emit(&bc, &door.p, .frame = bad_frames[i], .len = door.len, .h = door.h,
                                               .openings = door.open, .nr_openings = door.nr_open)))
            return EXIT_FAILURE;
    if (pcg_sink_get_stats(bad).nr_models)          return EXIT_FAILURE;

    /* more bays, or panes in a bay, than an int counts */
    z = c;
    z.p.bay = 1e-9f;
    cerr err = window_wall_case_emit(&bc, &z);
    if (!IS_CERR_CODE(err, CERR_TOO_LARGE))         return EXIT_FAILURE;
    z = c;
    z.p.mull_pitch = 1e-9f;
    err = window_wall_case_emit(&bc, &z);
    if (!IS_CERR_CODE(err, CERR_TOO_LARGE))         return EXIT_FAILURE;
    if (pcg_sink_get_stats(bad).nr_models)          return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int window_wall_surface_test(void)
{
    struct window_wall_case c = { .len = 10.0f, .h = 4.0f, .nr_open = 1 };
    pcg_window_wall_params_init(&c.p);
    c.open[0] = (pcg_opening){ .at = 5.0f, .w = 2.6f, .h = c.p.head };
    for (int s = 0; s < PCG_WINDOW_WALL_NR_SURF; s++)   c.p.surf[s].mat = &surf_mat[s];
    c.p.glazing.pane.mat = &surf_mat[PCG_WINDOW_WALL_NR_SURF];

    /* each part in its own bucket within its own heights, under a yawed and mirrored frame too */
    for (int m = 0; m < 2; m++) {
        pcg_frame f = PCG_FRAME_IDENTITY;
        if (m)  test_frame(&f, 2.2f, true, -3.0f, 4.0f, 7.0f);

        LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(window_wall_case_emit, &c, nullptr, &f),
                                             return EXIT_FAILURE);
        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != PCG_WINDOW_WALL_NR_SURF + 1)    return EXIT_FAILURE;

        struct mesh *spandrel = walk_mesh(&w, &surf_mat[PCG_WINDOW_WALL_SPANDREL]);
        struct mesh *header = walk_mesh(&w, &surf_mat[PCG_WINDOW_WALL_HEADER]);
        struct mesh *post = walk_mesh(&w, &surf_mat[PCG_WINDOW_WALL_POST]);
        struct mesh *mullion = walk_mesh(&w, &surf_mat[PCG_WINDOW_WALL_MULLION]);
        struct mesh *pane = walk_mesh(&w, &surf_mat[PCG_WINDOW_WALL_NR_SURF]);

        if (!spandrel || mesh_nr_vx(spandrel) != 2 * 36 || !mesh_y_within(spandrel, &f, 0.0f, c.p.sill))
            return EXIT_FAILURE;
        if (!header || mesh_nr_vx(header) != 36 || !mesh_y_within(header, &f, c.p.head, c.h))   return EXIT_FAILURE;
        if (!post || mesh_nr_vx(post) != 4 * 36 || !mesh_y_within(post, &f, 0.0f, c.p.head))    return EXIT_FAILURE;
        if (!mullion || mesh_nr_vx(mullion) != 4 * 36 || !mesh_y_within(mullion, &f, c.p.sill, c.p.head))
            return EXIT_FAILURE;
        if (!pane || mesh_nr_vx(pane) != 2 * 12 || !mesh_y_within(pane, &f, c.p.sill, c.p.head))    return EXIT_FAILURE;
        for (size_t v = 0; v < mesh_nr_vx(pane); v++) {
            vec3 p;

            pcg_frame_point_to_local(&f, &mesh_vx(pane)[v * 3], p);
            if (!near(p[2], 0.0f, 1e-5f))                                               return EXIT_FAILURE;
        }

        /* the solid parts aliased into one bucket, the union */
        struct window_wall_case a = c;
        for (int s = 0; s < PCG_WINDOW_WALL_NR_SURF; s++)   a.p.surf[s].mat = &panel;
        LOCAL_SET(pcg_sink, as) = CRES_RET(element_sink(window_wall_case_emit, &a, nullptr, &f),
                                           return EXIT_FAILURE);
        struct sink_walk aw;
        struct mesh *sep[PCG_WINDOW_WALL_NR_SURF] = { spandrel, header, post, mullion };
        if (!sink_walk_get(as, &aw) || aw.nr != 2)                                      return EXIT_FAILURE;
        struct mesh *solid = walk_mesh(&aw, &panel);
        if (!solid || !meshes_same_tris(&solid, 1, sep, PCG_WINDOW_WALL_NR_SURF))   return EXIT_FAILURE;

        /* skipped, the header and the glass are gone */
        a.p.header.skip = true;
        a.p.glazing.pane.skip = true;
        LOCAL_SET(pcg_sink, ks) = CRES_RET(element_sink(window_wall_case_emit, &a, nullptr, &f),
                                           return EXIT_FAILURE);
        struct mesh *kept[3] = { spandrel, post, mullion };
        if (!sink_walk_get(ks, &aw) || aw.nr != 1 || !meshes_same_tris(aw.mesh, 1, kept, 3))    return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/* a reference copy of the perimeter window wall emitter */
typedef struct ref_wall_params {
    vec2    org, dir;
    float   len, h, t;
    float   sill, head;
    float   mull_w, mull_pitch;
    float   post_w;
    int     bays;
    float   gap_at, gap_w;
    float   uv;
    bool    open_below;
} ref_wall_params;

static cerr ref_wall_emit(struct mesh *m, const ref_wall_params *p)
{
    float ht = p->t * 0.5f, lo = p->open_below ? 0.0f : p->sill;
    float g0 = p->gap_at - p->gap_w * 0.5f, g1 = p->gap_at + p->gap_w * 0.5f;
    bool gap = p->gap_w > 0.0f;

    if (!p->open_below) {
        if (gap) {
            CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, 0.0f, g0, 0.0f, p->sill, -ht, ht, p->uv));
            CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, g1, p->len, 0.0f, p->sill, -ht, ht, p->uv));
        } else {
            CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, 0.0f, p->len, 0.0f, p->sill, -ht, ht, p->uv));
        }
    }
    CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, 0.0f, p->len, p->head, p->h, -ht, ht, p->uv));

    CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, 0.0f, p->post_w, lo, p->head, -ht, ht, p->uv));
    CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, p->len - p->post_w, p->len, lo, p->head, -ht, ht, p->uv));
    if (gap) {
        CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, g0 - p->post_w, g0, 0.0f, p->head, -ht, ht, p->uv));
        CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, g1, g1 + p->post_w, 0.0f, p->head, -ht, ht, p->uv));
    }

    float bay = p->len / p->bays;
    for (int b = 1; b < p->bays; b++) {
        float at = b * bay;

        if (gap && at > g0 - p->post_w && at < g1 + p->post_w)  continue;
        CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, at - p->post_w * 0.5f, at + p->post_w * 0.5f, lo, p->head,
                                  -ht, ht, p->uv));
    }

    int k = (int)roundf(bay / p->mull_pitch);
    if (k < 1)  k = 1;
    for (int b = 0; b < p->bays; b++)
        for (int i = 1; i < k; i++) {
            float at = b * bay + i * bay / k;

            if (gap && at > g0 - p->mull_w && at < g1 + p->mull_w)  continue;
            CERR_RET_CERR(ref_dir_box(m, p->org, p->dir, at - p->mull_w * 0.5f, at + p->mull_w * 0.5f, lo, p->head,
                                      -ht, ht, p->uv));
        }

    return CERR_OK;
}

/* the reference building's facade measures */
#define REF_BAY         4.8f
#define REF_SILL_H      0.95f
#define REF_HEAD_H      3.05f
#define REF_ENTRANCE_W  2.6f

/*
 * The reference building's perimeter against reference copies of the
 * window wall and pane emitters, called the way the reference building
 * calls them: a 6-sided ring of 0.22 window walls on the 60 m outline
 * moved in by half their thickness, 5.65 tall on every storey's deck,
 * bays near 4.8 m, a band from 0.95 to 3.05 in 0.09 mullions near 1.2 m
 * apart between 0.3 posts, with the glass in it; the ground floor's side
 * 1 gets a 2.6 m entrance in the middle, the header carried over it.
 * Each side runs from one ring vertex toward the next.
 */
static int window_wall_parity_test(void)
{
    const int n = REF_SIDES;
    float apothem = REF_RADIUS * cosf((float)M_PI / n);
    float rin = REF_RADIUS * (apothem - REF_WALL_T * 0.5f) / apothem;
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    pcg_window_wall_params p;
    pcg_window_wall_params_init(&p);
    p.t = REF_WALL_T;
    p.sill = REF_SILL_H;
    p.head = REF_HEAD_H;
    p.bay = REF_BAY;
    p.mullion.role = PCG_ROLE_CLADDING;

    for (int fi = 0; fi < 2; fi++) {
        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("facade"), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, ref_walls) = CRES_RET(ref_new_checked(mesh, .name = "ref walls"), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, ref_glass) = CRES_RET(ref_new_checked(mesh, .name = "ref glass"), return EXIT_FAILURE);
        pcg_ctx ctx = { .sink = sink, .palette = &glazing_palette, .frame = frames[fi] };

        for (int f = 0; f < REF_FLOORS; f++) {
            float y = f * REF_FLOOR_H + REF_DECK_T;
            LOCAL_SET(mesh_t, mw) = CRES_RET(ref_new_checked(mesh, .name = "ref storey walls"), return EXIT_FAILURE);
            LOCAL_SET(mesh_t, mg) = CRES_RET(ref_new_checked(mesh, .name = "ref storey glass"), return EXIT_FAILURE);

            for (int i = 0; i < n; i++) {
                vec2 v0, v1, dir;

                ref_ngon_vertex(i, n, rin, v0);
                ref_ngon_vertex(i + 1, n, rin, v1);
                dir[0] = v1[0] - v0[0];
                dir[1] = v1[1] - v0[1];

                float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1]);

                dir[0] /= len;
                dir[1] /= len;

                int bays = (int)lrintf(len / REF_BAY);
                float gap_w = !f && i == REF_ENTRANCE ? REF_ENTRANCE_W : 0.0f;

                CERR_RET(ref_wall_emit(mw, &(ref_wall_params){
                             .org = { v0[0], v0[1] }, .dir = { dir[0], dir[1] },
                             .len = len, .h = REF_WALL_H, .t = REF_WALL_T,
                             .sill = REF_SILL_H, .head = REF_HEAD_H,
                             .mull_w = 0.09f, .mull_pitch = 1.2f, .post_w = 0.3f,
                             .bays = bays > 1 ? bays : 1,
                             .gap_at = len * 0.5f, .gap_w = gap_w,
                             .uv = REF_UV_PANEL,
                         }),
                         return EXIT_FAILURE);
                CERR_RET(ref_glass_emit(mg, v0, dir, len, REF_SILL_H, REF_HEAD_H, len * 0.5f, gap_w, 0.0f,
                                        REF_UV_BRONZE),
                         return EXIT_FAILURE);

                const pcg_opening entrance = { .at = len * 0.5f, .w = REF_ENTRANCE_W, .h = REF_HEAD_H };
                const pcg_frame at = { .org = { v0[0], y, v0[1] }, .dir = { dir[0], dir[1] } };
                CERR_RET(pcg_window_wall_emit(&ctx, &p, .frame = at, .len = len, .h = REF_WALL_H,
                                              .openings = &entrance, .nr_openings = gap_w > 0.0f),
                         return EXIT_FAILURE);
            }

            CERR_RET(ref_instance(ref_walls, mw, (vec3){ 0.0f, y, 0.0f }), return EXIT_FAILURE);
            CERR_RET(ref_instance(ref_glass, mg, (vec3){ 0.0f, y, 0.0f }), return EXIT_FAILURE);
        }

        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != 2)                                      return EXIT_FAILURE;
        if (!parity_check("facade walls", walk_mesh(&w, &panel), ref_walls, &frames[fi], false, REF_UV_PANEL))
            return EXIT_FAILURE;
        if (!parity_check("facade glass", walk_mesh(&w, &glass), ref_glass, &frames[fi], false, REF_UV_BRONZE))
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Fixture
 ****************************************************************************/

/* the reference layout's fixture finish: a whole tile per face */
static const pcg_material glow = { .name = "glow", .uv_mode = PCG_UV_UNIT, .skip_shadow = true };
static const pcg_palette fixture_palette = {
    .mat[PCG_ROLE_FIXTURE]  = &glow,
};

/* a fixture under a 3 m ceiling */
struct fixture_case {
    pcg_fixture_params  p;
    pcg_frame           frame;
    float               ceiling;
};

static void fixture_case_init(struct fixture_case *c)
{
    pcg_fixture_params_init(&c->p);
    c->frame = (pcg_frame){ .org = { 2.0f, 0.0f, -1.0f }, .dir = { 0.6f, 0.8f } };
    c->ceiling = 3.0f;
}

static cerr fixture_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct fixture_case *c = priv;

    return pcg_fixture_emit(ctx, &c->p, .frame = c->frame, .ceiling = c->ceiling);
}

static bool fixture_fails(const struct fixture_case *c)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("fixture"), return false);
    pcg_ctx ctx = { .sink = sink, .palette = &fixture_palette, .frame = PCG_FRAME_IDENTITY };
    struct sink_walk w;

    return failed_inval(fixture_case_emit(&ctx, c)) && sink_walk_get(sink, &w) && !w.nr;
}

static int fixture_test(void)
{
    struct fixture_case c;
    fixture_case_init(&c);

    if (c.p.body.role != PCG_ROLE_FIXTURE || c.p.body.collide != PCG_COLLIDE_NONE)  return EXIT_FAILURE;
    if (c.p.uv != PCG_UV_UNIT || c.p.drop != c.p.h)                                 return EXIT_FAILURE;

    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(fixture_case_emit, &c, &fixture_palette, &PCG_FRAME_IDENTITY),
                                         return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(sink, &w) || w.nr != 1 || w.key[0].mat != &glow)     return EXIT_FAILURE;
    if (w.key[0].collide != PCG_COLLIDE_NONE)                               return EXIT_FAILURE;
    if (mesh_nr_vx(w.mesh[0]) != (size_t)c.p.segments * 12)                 return EXIT_FAILURE;

    /* a closed disc flush with the ceiling, its underside the drop below it */
    if (!walk_closed(&w))                                                   return EXIT_FAILURE;
    double vol = cylinder_volume(c.p.segments, c.p.r, c.p.h);
    if (fabs(walk_volume(&w) - vol) > 1e-4 * vol)                           return EXIT_FAILURE;
    const vec3 lo = { -c.p.r, c.ceiling - c.p.drop, -c.p.r };
    const vec3 hi = { c.p.r, c.ceiling - c.p.drop + c.p.h, c.p.r };
    if (!walk_aabb_is(&w, &c.frame, lo, hi))                                return EXIT_FAILURE;

    if (!element_frames_ok(fixture_case_emit, &c, &fixture_palette, true))  return EXIT_FAILURE;

    /* bad styles and fits emit nothing */
    struct fixture_case bad = c;
    bad.p = (pcg_fixture_params){};
    if (!fixture_fails(&bad))                                               return EXIT_FAILURE;
    bad = c;
    bad.p.segments = 2;
    if (!fixture_fails(&bad))                                               return EXIT_FAILURE;
    bad = c;
    bad.p.r = -0.1f;
    if (!fixture_fails(&bad))                                               return EXIT_FAILURE;
    bad = c;
    bad.p.h = NAN;
    if (!fixture_fails(&bad))                                               return EXIT_FAILURE;
    bad = c;
    bad.p.drop = c.p.h * 0.5f;
    if (!fixture_fails(&bad))                                               return EXIT_FAILURE;
    bad = c;
    bad.p.uv = PCG_UV_NR;
    if (!fixture_fails(&bad))                                               return EXIT_FAILURE;
    bad = c;
    bad.ceiling = c.p.drop * 0.5f;
    if (!fixture_fails(&bad))                                               return EXIT_FAILURE;
    bad = c;
    bad.p.uv = PCG_UV_METERS;
    if (!fixture_fails(&bad))                                               return EXIT_FAILURE;
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = { &bad.p.r, &bad.p.h, &bad.p.drop, &bad.ceiling };

        for (size_t k = 0; k < array_size(fields); k++) {
            bad = c;
            *fields[k] = nonfinite[i];
            if (!fixture_fails(&bad))                                       return EXIT_FAILURE;
        }
    }
    for (size_t i = 0; i < array_size(bad_frames); i++) {
        bad = c;
        bad.frame = bad_frames[i];
        if (!fixture_fails(&bad))                                           return EXIT_FAILURE;
    }
    /* more segments than a mesh holds, also where segments * 12 wraps a 32-bit size_t: the check says so */
    const int many[] = { PCG_CYLINDER_SIDES_MAX + 1, 357913942 };
    for (size_t k = 0; k < array_size(many); k++) {
        bad = c;
        bad.p.segments = many[k];
        cerr err = pcg_fixture_check(&bad.p, &(pcg_fixture_site){ .frame = bad.frame, .ceiling = bad.ceiling });
        if (!IS_CERR_CODE(err, CERR_TOO_LARGE))                             return EXIT_FAILURE;
    }

    /* a longer drop hangs it lower */
    struct fixture_case low = c;
    low.p.drop = 0.3f;
    LOCAL_SET(pcg_sink, ls) = CRES_RET(element_sink(fixture_case_emit, &low, &fixture_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(ls, &w) || w.nr != 1)                                return EXIT_FAILURE;
    if (!mesh_y_within(w.mesh[0], &PCG_FRAME_IDENTITY, c.ceiling - low.p.drop, c.ceiling - low.p.drop + c.p.h))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int fixture_surface_test(void)
{
    struct fixture_case c;
    fixture_case_init(&c);

    /* a finish of its own, charted in meters */
    c.p.uv = PCG_UV_METERS;
    c.p.body.mat = &surf_mat[0];
    LOCAL_SET(pcg_sink, ms) = CRES_RET(element_sink(fixture_case_emit, &c, &fixture_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(ms, &w) || w.nr != 1 || w.key[0].mat != &surf_mat[0])    return EXIT_FAILURE;
    if (!element_frames_ok(fixture_case_emit, &c, &fixture_palette, true))      return EXIT_FAILURE;

    /* a collide class of its own */
    fixture_case_init(&c);
    c.p.body.collide = PCG_COLLIDE_SOLID;
    LOCAL_SET(pcg_sink, cs) = CRES_RET(element_sink(fixture_case_emit, &c, &fixture_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(cs, &w) || w.nr != 1 || w.key[0].collide != PCG_COLLIDE_SOLID)   return EXIT_FAILURE;

    /* skipped, nothing; without a finish for its role, an error */
    fixture_case_init(&c);
    c.p.body.skip = true;
    LOCAL_SET(pcg_sink, ss) = CRES_RET(element_sink(fixture_case_emit, &c, &fixture_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(ss, &w) || w.nr)                                         return EXIT_FAILURE;
    fixture_case_init(&c);
    c.p.body.role = PCG_ROLE_STRUCTURE;
    if (!fixture_fails(&c))                                                     return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* the reference building's ceiling over a room's floor: a storey less the slab and the deck */
#define REF_SOFFIT_H (REF_FLOOR_H - REF_SLAB_T - REF_DECK_T)

/* what the fixtures emit into, and the reference mesh they are compared against */
struct fixture_parity {
    pcg_ctx             ctx;
    struct mesh         *ref;
    pcg_fixture_params  fixture;
};

static cerr fixture_parity_ring(const ref_room_wall *rw, size_t nr, const ref_room_rec *rooms, size_t nr_rooms,
                                const ref_room_meshes *m, float y, void *priv)
{
    struct fixture_parity *fp = priv;

    CERR_RET_CERR(ref_instance(fp->ref, m->lights, (vec3){ 0.0f, y, 0.0f }));

    for (size_t k = 0; k < nr_rooms; k++) {
        const pcg_frame at = { .org = { rooms[k].light[0], y, rooms[k].light[1] },
                               .dir = { rooms[k].dir[0], rooms[k].dir[1] } };

        CERR_RET_CERR(pcg_fixture_emit(&fp->ctx, &fp->fixture, .frame = at, .ceiling = REF_SOFFIT_H));
    }

    return CERR_OK;
}

/*
 * The reference building's ceiling lights against the reference copy of
 * the room strip that emits them inline: one 12-sided disc per room,
 * 0.16 to its corners and 0.05 thick, its underside 0.10 below the top of
 * the 5.65 room walls, which reach 0.05 into the slab above, so the disc
 * sits flush with the soffit, every face carrying the whole texture. The
 * fixtures hang from the soffit itself, flush by default. The
 * originals stand square to the building, each fixture square to its
 * room: the rooms run at multiples of 60 degrees, which turn a 12-gon
 * onto itself.
 */
static int fixture_parity_test(void)
{
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    for (int fi = 0; fi < 2; fi++) {
        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("fixture"), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, ref) = CRES_RET(ref_new_checked(mesh, .name = "ref lights"), return EXIT_FAILURE);
        struct fixture_parity fp = {
            .ctx = { .sink = sink, .palette = &fixture_palette, .frame = frames[fi] },
            .ref = ref,
        };
        pcg_fixture_params_init(&fp.fixture);

        CERR_RET(ref_rooms_for_each(0, fixture_parity_ring, &fp), return EXIT_FAILURE);

        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != 1)                                              return EXIT_FAILURE;
        if (!parity_check("room lights", walk_mesh(&w, &glow), ref, &frames[fi], false, 1.0f))  return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Sign
 ****************************************************************************/

/* the reference layout's plates, and another finish for them */
static const pcg_palette sign_palette = {
    .mat[PCG_ROLE_SIGN]     = &decal,
};
static const pcg_material plaque_mat = { .name = "plaque", .uv_mode = PCG_UV_UNIT };

#define LABELS_MAX 128

/* what pcg_sink_for_each_label() reports */
struct label_walk {
    pcg_label   label[LABELS_MAX];
    size_t      nr;
};

static cerr label_walk_fn(const pcg_label *label, void *priv)
{
    struct label_walk *w = priv;

    if (w->nr == LABELS_MAX)    return CERR_TOO_LARGE;
    w->label[w->nr++] = *label;

    return CERR_OK;
}

/* @sink's labels; false if the walk fails or finds more than LABELS_MAX */
static bool label_walk_get(pcg_sink *sink, struct label_walk *w)
{
    w->nr = 0;

    return !failed(pcg_sink_for_each_label(sink, label_walk_fn, w));
}

/* a plaque on a 6 m wall, on its face or its pane, with or without a doorway in the middle */
struct sign_case {
    pcg_sign_params p;
    pcg_frame       frame;
    const char      *text;
    float           a0;
    float           len;
    pcg_opening     door;
    bool            has_door;
    bool            glass;
};

static void sign_case_init(struct sign_case *c)
{
    pcg_sign_params_init(&c->p);
    c->frame = (pcg_frame){ .org = { 2.0f, 0.0f, -1.0f }, .dir = { 0.6f, 0.8f } };
    c->a0 = 0.0f;
    c->len = 6.0f;
    c->door = (pcg_opening){ .at = 3.0f, .w = 1.1f, .h = 2.2f };
    c->text = "302";
    c->has_door = false;
    c->glass = false;
}

static cerr sign_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct sign_case *c = priv;

    return pcg_sign_emit(ctx, &c->p, .frame = c->frame, .a0 = c->a0, .len = c->len,
                         .door = c->has_door ? &c->door : nullptr, .text = c->text, .glass = c->glass);
}

/* @c recorded at @f, with its palette; the error if it fails */
static cresp(pcg_sink) sign_sink(const struct sign_case *c, const pcg_frame *f)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET_T(pcg_sink_new("sign"), pcg_sink);
    pcg_ctx ctx = { .sink = sink, .palette = &sign_palette, .frame = *f };

    CERR_RET_T(sign_case_emit(&ctx, c), pcg_sink);

    return cresp_val(pcg_sink, NOCU(sink));
}

static bool sign_fails(const struct sign_case *c)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("sign"), return false);
    pcg_ctx ctx = { .sink = sink, .palette = &sign_palette, .frame = PCG_FRAME_IDENTITY };

    return failed_inval(sign_case_emit(&ctx, c)) && !pcg_sink_get_stats(sink).nr_labels;
}

/* @c's one label at @f: its center, run direction and facing in @f's local coordinates */
static bool sign_local(const struct sign_case *c, const pcg_frame *f, pcg_label *l, vec3 org, vec3 dir, vec3 out)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(sign_sink(c, f), return false);
    struct label_walk w;

    /* nothing but the label, which is its atlas's model */
    if (!label_walk_get(sink, &w) || w.nr != 1 || pcg_sink_get_stats(sink).nr_models != 1)  return false;

    *l = w.label[0];
    l->text = nullptr;  /* the sink's */

    vec2 across;
    pcg_frame_across(&w.label[0].frame, across);
    pcg_frame_point_to_local(f, w.label[0].frame.org, org);
    pcg_frame_vector_to_local(f, (vec3){ w.label[0].frame.dir[0], 0.0f, w.label[0].frame.dir[1] }, dir);
    pcg_frame_vector_to_local(f, (vec3){ across[0], 0.0f, across[1] }, out);

    return !strcmp(w.label[0].text, c->text);
}

static int sign_test(void)
{
    struct sign_case c;
    sign_case_init(&c);

    if (c.p.plate.role != PCG_ROLE_SIGN || c.p.plate.collide != PCG_COLLIDE_NONE)   return EXIT_FAILURE;

    /* centered on the run, standing off the face, reading along it and facing out of it */
    pcg_label l;
    vec3 org, dir, out;
    if (!sign_local(&c, &PCG_FRAME_IDENTITY, &l, org, dir, out))            return EXIT_FAILURE;
    if (l.h != c.p.h || l.mat != &decal || l.collide != PCG_COLLIDE_NONE || l.chunk || l.frame.mirror)
        return EXIT_FAILURE;

    vec3 at;
    pcg_frame_point_to_local(&c.frame, org, at);
    if (!vec3_near(at, (vec3){ 3.0f, 1.5f, 0.02f }, 1e-5f)) return EXIT_FAILURE;
    pcg_frame_vector_to_local(&c.frame, dir, at);
    if (!vec3_near(at, (vec3){ 1.0f, 0.0f, 0.0f }, 1e-6f))  return EXIT_FAILURE;
    pcg_frame_vector_to_local(&c.frame, out, at);
    if (!vec3_near(at, (vec3){ 0.0f, 0.0f, 1.0f }, 1e-6f))  return EXIT_FAILURE;

    /* off a pane, beside a doorway */
    struct sign_case g = c;
    g.glass = true;
    g.has_door = true;
    if (!sign_local(&g, &PCG_FRAME_IDENTITY, &l, org, dir, out))            return EXIT_FAILURE;
    pcg_frame_point_to_local(&c.frame, org, at);
    if (!vec3_near(at, (vec3){ 3.0f + 0.55f + 0.3f, 1.5f, 0.05f }, 1e-5f))  return EXIT_FAILURE;

    /* a run that starts along its line */
    struct sign_case s = g;
    s.a0 = 1.5f;
    if (!sign_local(&s, &PCG_FRAME_IDENTITY, &l, org, dir, out))            return EXIT_FAILURE;
    pcg_frame_point_to_local(&c.frame, org, at);
    if (!vec3_near(at, (vec3){ 1.5f + 3.0f + 0.55f + 0.3f, 1.5f, 0.05f }, 1e-5f))   return EXIT_FAILURE;

    /* any frame carries it along; a mirrored one mirrors its frame, so it still faces out */
    vec3 ref_org, ref_dir, ref_out;
    if (!sign_local(&g, &PCG_FRAME_IDENTITY, &l, ref_org, ref_dir, ref_out))    return EXIT_FAILURE;
    for (int i = 0; i < 6; i++) {
        pcg_frame f;

        test_frame(&f, 0.9f * i - 1.3f, i & 1, 4.0f * i, 0.5f * i, -3.0f * i);
        if (!sign_local(&g, &f, &l, org, dir, out) || l.frame.mirror != f.mirror)   return EXIT_FAILURE;
        if (!vec3_near(org, ref_org, 1e-4f) || !vec3_near(dir, ref_dir, 1e-5f) || !vec3_near(out, ref_out, 1e-5f))
            return EXIT_FAILURE;
    }

    /* the same twice */
    pcg_label l2;
    vec3 org2, dir2, out2;
    pcg_frame f;
    test_frame(&f, 2.1f, true, -7.0f, 3.0f, 11.0f);
    if (!sign_local(&g, &f, &l, org, dir, out) || !sign_local(&g, &f, &l2, org2, dir2, out2))   return EXIT_FAILURE;
    if (!vec3_near(l.frame.org, l2.frame.org, 0.0f) || l.frame.dir[0] != l2.frame.dir[0] ||
        l.frame.dir[1] != l2.frame.dir[1] || l.frame.mirror != l2.frame.mirror || l.h != l2.h)
        return EXIT_FAILURE;

    /* bad styles and fits record nothing */
    struct sign_case bad = c;
    bad.p = (pcg_sign_params){};
    if (!sign_fails(&bad))                                                  return EXIT_FAILURE;
    bad = c;
    bad.p.h = NAN;
    if (!sign_fails(&bad))                                                  return EXIT_FAILURE;
    bad = c;
    bad.p.center_y = c.p.h * 0.4f;
    if (!sign_fails(&bad))                                                  return EXIT_FAILURE;
    bad = c;
    bad.p.standoff = -0.01f;
    if (!sign_fails(&bad))                                                  return EXIT_FAILURE;
    bad = c;
    bad.p.glass_standoff = -0.01f;
    if (!sign_fails(&bad))                                                  return EXIT_FAILURE;
    bad = c;
    bad.p.door_offset = -0.01f;
    if (!sign_fails(&bad))                                                  return EXIT_FAILURE;
    bad = c;
    bad.len = 0.0f;
    if (!sign_fails(&bad))                                                  return EXIT_FAILURE;
    bad = c;
    bad.a0 = INFINITY;
    if (!sign_fails(&bad))                                                  return EXIT_FAILURE;
    bad = c;
    bad.text = nullptr;
    if (!sign_fails(&bad))                                                  return EXIT_FAILURE;
    bad = c;
    bad.text = "";
    if (!sign_fails(&bad))                                                  return EXIT_FAILURE;
    bad = c;
    bad.has_door = true;
    bad.door.w = 7.0f;
    if (!sign_fails(&bad))                                                  return EXIT_FAILURE;
    /* a doorway that fits, but leaves no room past it */
    bad = c;
    bad.has_door = true;
    bad.door.at = 5.4f;
    if (!sign_fails(&bad))                                                  return EXIT_FAILURE;
    /* nothing that isn't finite, whether this sign would read it or not */
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = {
            &bad.p.h, &bad.p.center_y, &bad.p.standoff, &bad.p.glass_standoff, &bad.p.door_offset, &bad.a0,
            &bad.len,
        };

        for (size_t k = 0; k < array_size(fields); k++) {
            bad = c;
            *fields[k] = nonfinite[i];
            if (!sign_fails(&bad))                                          return EXIT_FAILURE;
        }
    }
    for (size_t i = 0; i < array_size(bad_frames); i++) {
        bad = c;
        bad.frame = bad_frames[i];
        if (!sign_fails(&bad))                                              return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int sign_surface_test(void)
{
    struct sign_case c;
    sign_case_init(&c);

    /* a finish and a collide class of its own */
    c.p.plate.mat = &plaque_mat;
    c.p.plate.collide = PCG_COLLIDE_SOLID;
    pcg_label l;
    vec3 org, dir, out;
    if (!sign_local(&c, &PCG_FRAME_IDENTITY, &l, org, dir, out))            return EXIT_FAILURE;
    if (l.mat != &plaque_mat || l.collide != PCG_COLLIDE_SOLID)             return EXIT_FAILURE;

    /* skipped, nothing */
    sign_case_init(&c);
    c.p.plate.skip = true;
    LOCAL_SET(pcg_sink, ss) = CRES_RET(sign_sink(&c, &PCG_FRAME_IDENTITY), return EXIT_FAILURE);
    if (pcg_sink_get_stats(ss).nr_labels)                                   return EXIT_FAILURE;

    /* a tiled finish, or none for its role: an error */
    sign_case_init(&c);
    c.p.plate.mat = &surf_mat[0];
    if (!sign_fails(&c))                                                    return EXIT_FAILURE;
    sign_case_init(&c);
    c.p.plate.role = PCG_ROLE_METAL;
    if (!sign_fails(&c))                                                    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* a plaque as the original mounted it: its center, run direction and deck */
struct ref_plaque {
    vec2    org, dir;
    float   y;
    char    text[16];
};

/* what the signs record into, and the original plaques they are compared against */
struct sign_parity {
    pcg_ctx             ctx;
    pcg_sign_params     sign;
    struct ref_plaque   ref[LABELS_MAX];
    size_t              nr_ref;
};

static cerr sign_parity_ring(const ref_room_wall *rw, size_t nr, const ref_room_rec *rooms, size_t nr_rooms,
                             const ref_room_meshes *m, float y, void *priv)
{
    struct sign_parity *sp = priv;

    for (size_t k = 0; k < nr_rooms; k++) {
        const ref_room_rec *rr = &rooms[k];

        if (!rr->has_plaque)                continue;
        if (sp->nr_ref == LABELS_MAX)       return CERR_TOO_LARGE;

        struct ref_plaque *rp = &sp->ref[sp->nr_ref++];
        *rp = (struct ref_plaque){
            .org = { rr->plaque[0], rr->plaque[1] }, .dir = { rr->dir[0], rr->dir[1] }, .y = y,
        };
        memcpy(rp->text, rr->text, sizeof(rp->text));

        /* the front wall's run, on its face or its pane, as the room strip lays it out */
        vec2 in = { -rr->dir[1], rr->dir[0] };
        float r0 = rr->a0 + rr->ins_l, r1 = rr->a1 - rr->ins_r, len = r1 - r0;
        float face = rr->front_glass ? rr->cf - REF_ROOM_T * 0.5f : rr->cf;
        const pcg_frame at = {
            .org = { rr->v0[0] + in[0] * face, y, rr->v0[1] + in[1] * face },
            .dir = { rr->dir[0], rr->dir[1] },
        };
        const pcg_opening door = { .at = len * 0.5f, .w = REF_DOOR_W, .h = REF_DOOR_H };

        CERR_RET_CERR(pcg_sign_emit(&sp->ctx, &sp->sign, .frame = at, .a0 = r0, .len = len,
                                    .door = rr->front_door ? &door : nullptr, .text = rr->text,
                                    .glass = rr->front_glass));
    }

    return CERR_OK;
}

/* a plate as wide as some text 2.4 times as wide as it is high */
#define SIGN_ASPECT 2.4f

/*
 * How far label @l, placed under @f, lands from plaque @rp: its quad's
 * corners into *@err, its facing into *@nerr, each the larger of the
 * two; false if its text, height or mirroring differs. The quad is
 * built the way the commit builds a label's, around its frame, and the
 * original's the way the original mounted it, both with SIGN_ASPECT.
 */
static bool plaque_near(const pcg_label *l, const struct ref_plaque *rp, const pcg_frame *f, float *err,
                        float *nerr)
{
    float h = REF_PLAQUE_H, ha = h * SIGN_ASPECT * 0.5f;
    float y0 = REF_PLAQUE_Y - h * 0.5f, y1 = y0 + h;

    if (strcmp(l->text, rp->text) || l->h != h || l->frame.mirror)  return false;

    /* the original quad's corners c00, c10, c11, c01 and the label's */
    const float ca[4] = { ha, -ha, -ha, ha }, cy[4] = { y0, y0, y1, y1 };
    const float ly[4] = { -h * 0.5f, -h * 0.5f, h * 0.5f, h * 0.5f };
    vec3 want[4], got[4];

    for (int j = 0; j < 4; j++) {
        vec3 p = { rp->org[0] + ca[j] * rp->dir[0], rp->y + cy[j], rp->org[1] + ca[j] * rp->dir[1] };

        pcg_frame_point(f, p, want[j]);
        pcg_frame_point(&l->frame, (vec3){ ca[j], ly[j], 0.0f }, got[j]);
        *err = fmaxf(*err, max_diff(want[j], got[j], 3));
    }

    /* it faces where the original's normal points: cross(c01 - c00, c10 - c00) */
    vec3 e0, e1, n;
    vec2 across;
    vec3_sub(e0, want[3], want[0]);
    vec3_sub(e1, want[1], want[0]);
    vec3_mul_cross(n, e0, e1);
    vec3_norm(n, n);
    pcg_frame_across(&l->frame, across);
    vec3 facing = { across[0], 0.0f, across[1] };
    *nerr = fmaxf(*nerr, max_diff(n, facing, 3));

    return true;
}

/*
 * The reference building's room plaques against the reference copy of the
 * room strip that records them, mounted as the original mounted them: a
 * quad 0.32 high centered 1.5 over the deck, 0.05 proud of a glass front's
 * pane or 0.02 of a solid front's face, centered on the front's run or
 * 0.30 past its doorway, reading along the run and facing out of the
 * room. Signs only record labels, so their quads are built the way the
 * commit builds a label's, around its frame, with the same text aspect
 * for both. The texts are the original's room numbers. Both the
 * reference rooms and every wall solid and doored front and back.
 */
static int sign_parity_test(void)
{
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    for (int fi = 0; fi < 2; fi++)
        for (int variant = 0; variant < 2; variant++) {
            const pcg_frame *f = &frames[fi];
            LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("sign"), return EXIT_FAILURE);
            LOCAL_SET(void, buf) = mem_alloc(sizeof(struct sign_parity), .zero = 1);
            struct sign_parity *sp = buf;
            if (!sp)    return EXIT_FAILURE;

            sp->ctx = (pcg_ctx){ .sink = sink, .palette = &sign_palette, .frame = *f };
            pcg_sign_params_init(&sp->sign);

            CERR_RET(ref_rooms_for_each(variant, sign_parity_ring, sp), return EXIT_FAILURE);

            LOCAL_SET(void, wbuf) = mem_alloc(sizeof(struct label_walk));
            struct label_walk *w = wbuf;
            if (!w || !label_walk_get(sink, w) || w->nr != sp->nr_ref)  return EXIT_FAILURE;
            /* the reference building has 97 plaques */
            if (!variant && w->nr != 97)                                return EXIT_FAILURE;

            float err = 0.0f, nerr = 0.0f;
            for (size_t k = 0; k < w->nr; k++)
                if (!plaque_near(&w->label[k], &sp->ref[k], f, &err, &nerr))    return EXIT_FAILURE;

            if (verbose)    printf("plaques: %zu, max error p %g n %g\n", w->nr, err, nerr);
            if (err > PARITY_EPS || nerr > PARITY_DIR_EPS)              return EXIT_FAILURE;
        }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Skylight
 ****************************************************************************/

/* the reference layout's skylight finishes: bronze beams under a tiled glow */
static const pcg_material glow_grid = { .name = "glow grid", .uv_mode = PCG_UV_METERS };
static const pcg_palette skylight_palette = {
    .mat[PCG_ROLE_METAL]    = &bronze,
    .mat[PCG_ROLE_DIFFUSER] = &glow_grid,
};

/* a skylight over a hexagonal aperture, on a roof 24 m up */
struct skylight_case {
    pcg_skylight_params p;
    pcg_frame           frame;
    int                 sides;
    float               r;
};

static void skylight_case_init(struct skylight_case *c, float r)
{
    pcg_skylight_params_init(&c->p);
    c->frame = (pcg_frame){ .org = { 1.0f, 24.0f, -2.0f }, .dir = { 1.0f, 0.0f } };
    c->sides = 6;
    c->r = r;
}

static cerr skylight_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct skylight_case *c = priv;

    return pcg_skylight_emit(ctx, &c->p, .frame = c->frame, .sides = c->sides, .r = c->r);
}

static bool skylight_fails(const struct skylight_case *c, cerr_enum code)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("skylight"), return false);
    pcg_ctx ctx = { .sink = sink, .palette = &skylight_palette, .frame = PCG_FRAME_IDENTITY };
    struct sink_walk w;
    cerr err = skylight_case_emit(&ctx, c);

    return failed(err) && CERR_CODE(err) == code && sink_walk_get(sink, &w) && !w.nr;
}

/* @c's beams by the rule, with their volume: chords of at least a beam's width, a bay apart */
static size_t skylight_beams(const struct skylight_case *c, double *vol)
{
    vec2 ap[16], lo, hi;
    size_t nr = 0;

    poly2d_regular(ap, c->sides, c->r, poly2d_phase_edge0_neg_z(c->sides));
    poly2d_bbox(ap, c->sides, lo, hi);
    *vol = 0.0;

    for (int axis = 0; axis < 2; axis++) {
        int o = axis ^ 1, n = (int)roundf((hi[o] - lo[o]) / c->p.pitch);
        float top = axis ? c->p.curb_h - c->p.cross_drop : c->p.curb_h;

        for (int i = 1; i < n; i++) {
            float at = lo[o] + i * (hi[o] - lo[o]) / n, a0, a1;
            float len = axis ? poly2d_chord(ap, c->sides, (vec2){}, (vec2){ 0.0f, 1.0f }, -at, &a0, &a1)
                             : poly2d_chord(ap, c->sides, (vec2){}, (vec2){ 1.0f, 0.0f }, at, &a0, &a1);

            if (len < c->p.beam_w)  continue;
            nr++;
            *vol += (double)(len + 2.0f * c->p.embed) * c->p.beam_w * (top - (c->p.curb_h - c->p.beam_h));
        }
    }

    return nr;
}

/*
 * @c's diffuser @m, emitted at @f, charts a pitch per bay from the
 * aperture's bounding box, in @c's frame
 */
static bool skylight_chart_ok(const struct skylight_case *c, struct mesh *m, const pcg_frame *f)
{
    vec2 ap[16], lo, hi;

    poly2d_regular(ap, c->sides, c->r, poly2d_phase_edge0_neg_z(c->sides));
    poly2d_bbox(ap, c->sides, lo, hi);

    pcg_frame at;
    pcg_frame_compose(f, &c->frame, &at);
    for (size_t v = 0; m && v < mesh_nr_vx(m); v++) {
        vec3 p;

        pcg_frame_point_to_local(&at, &mesh_vx(m)[v * 3], p);
        for (int k = 0; k < 2; k++) {
            int bays = (int)roundf((hi[k] - lo[k]) / c->p.pitch);
            float x = k ? p[2] : p[0], step = (hi[k] - lo[k]) / (bays > 1 ? bays : 1);

            if (!near(mesh_tx(m)[v * 2 + k], (x - lo[k]) / step * c->p.pitch, 1e-4f))   return false;
        }
    }

    return m;
}

static int skylight_test(void)
{
    struct skylight_case c;
    skylight_case_init(&c, 6.0f);

    if (c.p.frame.role != PCG_ROLE_METAL || c.p.frame.collide != PCG_COLLIDE_SOLID)         return EXIT_FAILURE;
    if (c.p.diffuser.role != PCG_ROLE_DIFFUSER || c.p.diffuser.collide != PCG_COLLIDE_NONE) return EXIT_FAILURE;

    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(skylight_case_emit, &c, &skylight_palette,
                                                      &PCG_FRAME_IDENTITY),
                                         return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(sink, &w) || w.nr != 2)                              return EXIT_FAILURE;
    struct mesh *fm = walk_mesh(&w, &bronze), *dm = walk_mesh(&w, &glow_grid);
    if (!fm || !dm)                                                         return EXIT_FAILURE;

    /* the frame: a closed curb around the aperture, the beams over it */
    double beams_vol;
    size_t nr_beams = skylight_beams(&c, &beams_vol);
    float apothem = c.r * cosf((float)M_PI / c.sides), r_out = c.r * (apothem + c.p.curb_w) / apothem;
    double curb_vol = (ngon_area(c.sides, r_out) - ngon_area(c.sides, c.r)) * c.p.curb_h;
    struct sink_walk fw = { .key = { w.key[0] }, .mesh = { fm }, .nr = 1 };

    /* the curb: 12 triangles top and bottom, 12 edge quads */
    if (nr_beams < 4 || mesh_nr_vx(fm) != 2 * 12 * 3 + 12 * 6 + nr_beams * 36)              return EXIT_FAILURE;
    if (!walk_closed(&fw))                                                                  return EXIT_FAILURE;
    if (fabs(walk_volume(&fw) - (curb_vol + beams_vol)) > 1e-4 * (curb_vol + beams_vol))    return EXIT_FAILURE;

    vec2 outer[6], olo, ohi;
    poly2d_regular(outer, c.sides, r_out, poly2d_phase_edge0_neg_z(c.sides));
    poly2d_bbox(outer, c.sides, olo, ohi);
    if (!walk_aabb_is(&fw, &c.frame, (vec3){ olo[0], 0.0f, olo[1] }, (vec3){ ohi[0], c.p.curb_h, ohi[1] }))
        return EXIT_FAILURE;

    /* the diffuser: one downward sheet over the curb's outline, lifted over it */
    struct sink_walk dw = { .key = { w.key[1] }, .mesh = { dm }, .nr = 1 };
    float y = c.p.curb_h + c.p.lift;
    if (mesh_nr_vx(dm) != (size_t)(c.sides - 2) * 3)                                            return EXIT_FAILURE;
    if (!mesh_normals_along(dm, &c.frame, (vec3){ 0.0f, -1.0f, 0.0f }))                         return EXIT_FAILURE;
    if (!walk_aabb_is(&dw, &c.frame, (vec3){ olo[0], y, olo[1] }, (vec3){ ohi[0], y, ohi[1] })) return EXIT_FAILURE;

    /* a pitch of texture per bay, so the beam lines fall on whole multiples of it */
    if (!skylight_chart_ok(&c, dm, &PCG_FRAME_IDENTITY))                    return EXIT_FAILURE;

    /* the same under any frame, the diffuser's chart included */
    if (!element_frames_ok(skylight_case_emit, &c, &skylight_palette, false))   return EXIT_FAILURE;
    for (int mirror = 0; mirror < 2; mirror++) {
        pcg_frame f;

        test_frame(&f, 2.3f, mirror, -5.0f, 1.0f, 8.0f);
        LOCAL_SET(pcg_sink, ys) = CRES_RET(element_sink(skylight_case_emit, &c, &skylight_palette, &f),
                                           return EXIT_FAILURE);
        struct sink_walk yw;
        if (!sink_walk_get(ys, &yw) || yw.nr != 2)                          return EXIT_FAILURE;
        if (!skylight_chart_ok(&c, walk_mesh(&yw, &glow_grid), &f))         return EXIT_FAILURE;
    }

    /* an aperture too small for a beam: the curb and the diffuser, a bay of texture across */
    struct skylight_case small;
    skylight_case_init(&small, 0.2f);
    LOCAL_SET(pcg_sink, ss) = CRES_RET(element_sink(skylight_case_emit, &small, &skylight_palette,
                                                    &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(ss, &w) || w.nr != 2)                                return EXIT_FAILURE;
    if (mesh_nr_vx(walk_mesh(&w, &bronze)) != 2 * 12 * 3 + 12 * 6)          return EXIT_FAILURE;

    /* bad styles and fits emit nothing */
    const cerr_enum inval = _CERR_INVALID_ARGUMENTS;
    struct skylight_case bad = c;
    bad.p = (pcg_skylight_params){};
    if (!skylight_fails(&bad, inval))                                       return EXIT_FAILURE;
    bad = c;
    bad.p.curb_w = 0.0f;
    if (!skylight_fails(&bad, inval))                                       return EXIT_FAILURE;
    bad = c;
    bad.p.beam_h = -0.4f;
    if (!skylight_fails(&bad, inval))                                       return EXIT_FAILURE;
    bad = c;
    bad.p.pitch = NAN;
    if (!skylight_fails(&bad, inval))                                       return EXIT_FAILURE;
    bad = c;
    bad.p.embed = c.p.curb_w * 2.0f;
    if (!skylight_fails(&bad, inval))                                       return EXIT_FAILURE;
    bad = c;
    bad.p.cross_drop = c.p.beam_h;
    if (!skylight_fails(&bad, inval))                                       return EXIT_FAILURE;
    bad = c;
    bad.p.lift = -0.1f;
    if (!skylight_fails(&bad, inval))                                       return EXIT_FAILURE;
    bad = c;
    bad.sides = 2;
    if (!skylight_fails(&bad, inval))                                       return EXIT_FAILURE;
    bad = c;
    bad.r = 0.0f;
    if (!skylight_fails(&bad, inval))                                       return EXIT_FAILURE;
    bad = c;
    bad.p.pitch = c.p.beam_w * 0.7f;
    if (!skylight_fails(&bad, inval))                                       return EXIT_FAILURE;
    bad = c;
    bad.p.pitch = 1e-5f;
    if (!skylight_fails(&bad, inval))                                       return EXIT_FAILURE;
    bad = c;
    bad.p.pitch = 1e-30f;
    if (!skylight_fails(&bad, _CERR_TOO_LARGE))                             return EXIT_FAILURE;
    /* nothing that isn't finite, framed or not */
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = {
            &bad.p.curb_w, &bad.p.curb_h, &bad.p.beam_w, &bad.p.beam_h, &bad.p.pitch, &bad.p.embed,
            &bad.p.cross_drop, &bad.p.lift, &bad.r,
        };

        for (size_t k = 0; k < array_size(fields); k++) {
            bad = c;
            *fields[k] = nonfinite[i];
            if (!skylight_fails(&bad, inval))                               return EXIT_FAILURE;
            bad = c;
            bad.p.frame.skip = true;
            *fields[k] = nonfinite[i];
            if (!skylight_fails(&bad, inval))                               return EXIT_FAILURE;
        }
    }
    for (size_t i = 0; i < array_size(bad_frames); i++) {
        bad = c;
        bad.frame = bad_frames[i];
        if (!skylight_fails(&bad, inval))                                   return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int skylight_surface_test(void)
{
    struct skylight_case c;
    skylight_case_init(&c, 6.0f);

    /* each surface on its own: the frame without the diffuser, the diffuser without the frame */
    for (int k = 0; k < PCG_SKYLIGHT_NR_SURF; k++) {
        struct skylight_case s = c;
        s.p.surf[k].skip = true;

        LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(skylight_case_emit, &s, &skylight_palette,
                                                          &PCG_FRAME_IDENTITY),
                                             return EXIT_FAILURE);
        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != 1)                              return EXIT_FAILURE;
        if (w.key[0].mat != (k == PCG_SKYLIGHT_FRAME ? &glow_grid : &bronze))   return EXIT_FAILURE;
    }

    /* explicit finishes and collide classes */
    struct skylight_case e = c;
    e.p.frame.mat = &surf_mat[0];
    e.p.frame.collide = PCG_COLLIDE_HAZARD;
    e.p.diffuser.mat = &surf_mat[1];
    LOCAL_SET(pcg_sink, es) = CRES_RET(element_sink(skylight_case_emit, &e, &skylight_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(es, &w) || w.nr != 2)                                    return EXIT_FAILURE;
    if (w.key[0].mat != &surf_mat[0] || w.key[0].collide != PCG_COLLIDE_HAZARD) return EXIT_FAILURE;
    if (w.key[1].mat != &surf_mat[1] || w.key[1].collide != PCG_COLLIDE_NONE)   return EXIT_FAILURE;

    /* both on one finish: one bucket, the union of the two */
    struct skylight_case one = c;
    one.p.diffuser.role = PCG_ROLE_METAL;
    one.p.diffuser.collide = PCG_COLLIDE_SOLID;
    LOCAL_SET(pcg_sink, os) = CRES_RET(element_sink(skylight_case_emit, &one, &skylight_palette,
                                                    &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    LOCAL_SET(pcg_sink, ts) = CRES_RET(element_sink(skylight_case_emit, &c, &skylight_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    struct sink_walk ow, tw;
    if (!sink_walk_get(os, &ow) || ow.nr != 1 || !sink_walk_get(ts, &tw) || tw.nr != 2) return EXIT_FAILURE;
    if (!meshes_same_tris(ow.mesh, 1, tw.mesh, 2))                                      return EXIT_FAILURE;

    /* a decal finish on a charted surface: an error */
    struct skylight_case d = c;
    d.p.diffuser.mat = &decal;
    LOCAL_SET(pcg_sink, ds) = CRES_RET(pcg_sink_new("skylight"), return EXIT_FAILURE);
    pcg_ctx dc = { .sink = ds, .palette = &skylight_palette, .frame = PCG_FRAME_IDENTITY };
    if (!failed(skylight_case_emit(&dc, &d)))                               return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* reference copies of the original's n-gon chord and bounding box */
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

typedef struct ref_skylight_params {
    int     sides;
    float   r;
    float   beam_w, beam_h;
    float   pitch;
    float   curb_w, curb_h;
    float   uv;
} ref_skylight_params;

/*
 * A reference copy of the coffer grid emitter: the curb plate, then the
 * chord-clipped beams both ways; the curb goes to @curb, the beams to
 * @beams, so their charts can be placed apart
 */
static cerr ref_skylight_emit(struct mesh *curb, struct mesh *beams, const ref_skylight_params *p)
{
    float a = p->r * cosf((float)M_PI / p->sides);
    float embed = fminf(p->curb_w, 0.08f);
    vec2 lo, hi;

    CERR_RET_CERR(ref_plate_emit(curb, p->sides, p->r * (a + p->curb_w) / a, p->sides, p->r, 0.0f, p->curb_h,
                                 p->uv, 0));

    ref_poly_bbox(p->sides, p->r, lo, hi);

    int nz = (int)roundf((hi[1] - lo[1]) / p->pitch);
    for (int i = 1; i < nz; i++) {
        float z = lo[1] + i * (hi[1] - lo[1]) / nz;
        float x0, x1;

        if (ref_poly_span(p->sides, p->r, 0, z, &x0, &x1) < p->beam_w)  continue;
        CERR_RET_CERR(ref_arch_box(beams, (vec3){ x0 - embed, p->curb_h - p->beam_h, z - p->beam_w * 0.5f },
                                   (vec3){ x1 + embed, p->curb_h, z + p->beam_w * 0.5f }, p->uv));
    }

    int nx = (int)roundf((hi[0] - lo[0]) / p->pitch);
    for (int i = 1; i < nx; i++) {
        float x = lo[0] + i * (hi[0] - lo[0]) / nx;
        float z0, z1;

        if (ref_poly_span(p->sides, p->r, 1, x, &z0, &z1) < p->beam_w)  continue;
        CERR_RET_CERR(ref_arch_box(beams, (vec3){ x - p->beam_w * 0.5f, p->curb_h - p->beam_h, z0 - embed },
                                   (vec3){ x + p->beam_w * 0.5f, p->curb_h - 0.03f, z1 + embed }, p->uv));
    }

    return CERR_OK;
}

/* a reference copy of the diffuser panel: a downward-facing n-gon, UV 0..1 over its bounding box */
static cerr ref_panel_emit(struct mesh *m, int sides, float r)
{
    vec2 lo, hi;

    ref_poly_bbox(sides, r, lo, hi);

    CRES_RET_CERR(_prim_begin((sides - 2) * 3, &(prim_emit_opts){ .mesh = m, .tangents = true }));

    prim_emit_opts po = { .mesh = m };
    vec2 p0;

    ref_ngon_vertex(0, sides, r, p0);
    for (int k = 1; k < sides - 1; k++) {
        vec2 pk, pk1;

        ref_ngon_vertex(k, sides, r, pk);
        ref_ngon_vertex(k + 1, sides, r, pk1);

        vec3 v[3] = { { p0[0], 0.0f, p0[1] }, { pk[0], 0.0f, pk[1] }, { pk1[0], 0.0f, pk1[1] } };
        vec2 tuv[3];

        for (int i = 0; i < 3; i++) {
            tuv[i][0] = (v[i][0] - lo[0]) / (hi[0] - lo[0]);
            tuv[i][1] = (v[i][2] - lo[1]) / (hi[1] - lo[1]);
        }
        po.uv = tuv;
        _prim_emit_triangle3(v[0], v[1], v[2], &po);
    }

    return CERR_OK;
}

/* the reference building's skylight measures */
#define REF_CURB_W      0.25f
#define REF_CURB_H      0.5f
#define REF_DIFFUSER_UP 0.12f

/*
 * The reference building's skylight against reference copies of the
 * coffer grid and diffuser emitters, as the reference building places
 * them on its roof, 24 m up, over the top well of 28: a 0.25 curb 0.5
 * high, 0.07 beams 0.4 deep at a 1.2 pitch, bearing 0.08 into the curb,
 * the cross beams 0.03 lower, bronze at 1.2 m per repeat; and the
 * diffuser 0.12 over the curb, as wide as it. The curb charts like a
 * plate and the beams like boxes, so the expected curb faces re-chart
 * where a frame places them while the beams carry theirs. The diffuser's
 * chart follows the beam grid now, an accepted change: only its shape is
 * compared.
 */
static int skylight_parity_test(void)
{
    const float roof = REF_FLOORS * REF_FLOOR_H, r = ref_well_r[REF_FLOORS - 1];
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    pcg_skylight_params p;
    pcg_skylight_params_init(&p);

    LOCAL_SET(mesh_t, curb) = CRES_RET(ref_new_checked(mesh, .name = "ref curb"), return EXIT_FAILURE);
    LOCAL_SET(mesh_t, beams) = CRES_RET(ref_new_checked(mesh, .name = "ref beams"), return EXIT_FAILURE);
    LOCAL_SET(mesh_t, panel) = CRES_RET(ref_new_checked(mesh, .name = "ref panel"), return EXIT_FAILURE);
    float wa = r * cosf((float)M_PI / REF_WELL_SIDES);

    CERR_RET(ref_skylight_emit(curb, beams, &(ref_skylight_params){
                 .sides = REF_WELL_SIDES, .r = r, .beam_w = 0.07f, .beam_h = 0.4f, .pitch = 1.2f,
                 .curb_w = REF_CURB_W, .curb_h = REF_CURB_H, .uv = REF_UV_BRONZE,
             }),
             return EXIT_FAILURE);
    CERR_RET(ref_panel_emit(panel, REF_WELL_SIDES, r * (wa + REF_CURB_W) / wa), return EXIT_FAILURE);

    /* placed on the roof, as the original's entities were */
    LOCAL_SET(mesh_t, rc) = CRES_RET(ref_new_checked(mesh, .name = "ref roof curb"), return EXIT_FAILURE);
    LOCAL_SET(mesh_t, rb) = CRES_RET(ref_new_checked(mesh, .name = "ref roof beams"), return EXIT_FAILURE);
    LOCAL_SET(mesh_t, rp) = CRES_RET(ref_new_checked(mesh, .name = "ref roof panel"), return EXIT_FAILURE);
    CERR_RET(ref_instance(rc, curb, (vec3){ 0.0f, roof, 0.0f }), return EXIT_FAILURE);
    CERR_RET(ref_instance(rb, beams, (vec3){ 0.0f, roof, 0.0f }), return EXIT_FAILURE);
    CERR_RET(ref_instance(rp, panel, (vec3){ 0.0f, roof + REF_CURB_H + REF_DIFFUSER_UP, 0.0f }),
             return EXIT_FAILURE);

    for (int fi = 0; fi < 2; fi++) {
        const pcg_frame *f = &frames[fi];
        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("skylight"), return EXIT_FAILURE);
        pcg_ctx ctx = { .sink = sink, .palette = &skylight_palette, .frame = *f };
        const pcg_frame at = { .org = { 0.0f, roof, 0.0f }, .dir = { 1.0f, 0.0f } };

        CERR_RET(pcg_skylight_emit(&ctx, &p, .frame = at, .sides = REF_WELL_SIDES, .r = r), return EXIT_FAILURE);

        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != 2)                          return EXIT_FAILURE;

        /* the curb's horizontal faces re-chart where @f puts them, the beams' come along */
        LOCAL_SET(mesh_t, ec) = CRES_RET(parity_expect(rc, f, true, REF_UV_BRONZE), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, eb) = CRES_RET(parity_expect(rb, f, false, REF_UV_BRONZE), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, expect) = CRES_RET(ref_new_checked(mesh, .name = "expect"), return EXIT_FAILURE);
        CERR_RET(ref_instance(expect, ec, (vec3){}), return EXIT_FAILURE);
        CERR_RET(ref_instance(expect, eb, (vec3){}), return EXIT_FAILURE);

        if (!parity_check("skylight frame", walk_mesh(&w, &bronze), expect, &PCG_FRAME_IDENTITY, false,
                          REF_UV_BRONZE))
            return EXIT_FAILURE;
        if (!parity_check_shape("skylight diffuser", walk_mesh(&w, &glow_grid), rp, f)) return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Flight
 ****************************************************************************/

/* the arrival apron's own finish, so that its plate charts compare apart from the stair's box charts */
static const pcg_material apron = { .name = "apron", .uv_mode = PCG_UV_METERS };

/* a flight at a site in its parent, with rectangular voids given in the site frame's (a, c) */
struct flight_case {
    pcg_flight_params   p;
    pcg_frame           frame;
    float               len;
    float               rise;
    vec2                depart[4];
    size_t              nr_depart;
    vec2                arrive[4];
    size_t              nr_arrive;
};

/* the void [@a0, @a1] by [@c0, @c1] of @f's (a, c), in its parent's (x, z) */
static void flight_void(const pcg_frame *f, float a0, float a1, float c0, float c1, vec2 *out)
{
    const float at[4][2] = { { a0, c0 }, { a1, c0 }, { a1, c1 }, { a0, c1 } };

    for (int k = 0; k < 4; k++) {
        vec3 p;

        pcg_frame_point(f, (vec3){ at[k][0], 0.0f, at[k][1] }, p);
        out[k][0] = p[0];
        out[k][1] = p[2];
    }
}

/*
 * A 3 m climb in an 8 m chord at @f: 18 risers of 0.28 leave 1.48 m
 * landings; both floors' voids reach from the chord 4 m out and 1 m past
 * either end of it
 */
static void flight_case_init(struct flight_case *c, const pcg_frame *f)
{
    pcg_flight_params_init(&c->p);
    c->p.arrive.mat = &apron;
    c->frame = *f;
    c->len = 8.0f;
    c->rise = 3.0f;
    flight_void(f, -1.0f, 9.0f, 0.0f, 4.0f, c->depart);
    flight_void(f, -1.0f, 9.0f, 0.0f, 4.0f, c->arrive);
    c->nr_depart = c->nr_arrive = 4;
}

static void flight_case_site(const struct flight_case *c, pcg_flight_site *s)
{
    *s = (pcg_flight_site){
        .frame          = c->frame,
        .len            = c->len,
        .rise           = c->rise,
        .depart_void    = c->nr_depart ? c->depart : nullptr,
        .nr_depart_void = c->nr_depart,
        .arrive_void    = c->nr_arrive ? c->arrive : nullptr,
        .nr_arrive_void = c->nr_arrive,
    };
}

static cerr flight_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct flight_case *c = priv;
    pcg_flight_site s;

    flight_case_site(c, &s);

    return _pcg_flight_emit(ctx, &c->p, &s);
}

/* @m alone, as a walk */
static struct sink_walk walk_one(struct mesh *m)
{
    struct sink_walk w = { .nr = 1 };

    w.mesh[0] = m;

    return w;
}

/*
 * The buckets of @c's flight: the stair, the aprons over [-1, land] and
 * [len - land, 9] of its frame out to the flight's far edge, @sloped
 * sloped rails and @landings landing rails, from @c's rim clearance past
 * the voids' ends to the run
 */
static bool flight_case_ok(const struct flight_case *c, bool depart, bool arrive, int sloped, int landings)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(flight_case_emit, c, &rail_palette, &PCG_FRAME_IDENTITY),
                                         return false);
    struct sink_walk w;
    if (!sink_walk_get(sink, &w))   return false;

    const pcg_flight_params *p = &c->p;
    int steps = CRES_RET(pcg_stair_steps(&p->stair, c->rise), return false);
    float run = steps * p->stair.going, land = (c->len - run) * 0.5f, deep = p->gap + p->stair.width;
    float lt = p->stair.landing_t;

    struct mesh *stair = walk_mesh(&w, &concrete), *rail = walk_mesh(&w, &slat);
    struct mesh *dm = walk_mesh(&w, &wood), *am = walk_mesh(&w, &apron);
    if (w.nr != 2u + depart + arrive || !stair || !rail || !dm != !depart || !am != !arrive)    return false;
    if (mesh_nr_vx(stair) != (size_t)steps * 36)                                                return false;

    struct sink_walk sw = walk_one(stair);
    struct stair_case sc = { .p = p->stair, .rise = c->rise };
    double svol = stair_volume(&sc, steps);
    if (!walk_closed(&sw) || fabs(walk_volume(&sw) - svol) > 1e-4 * svol)                   return false;

    /* the aprons are rectangles here: two fan triangles a side and four edges */
    const struct { struct mesh *m; float a0, a1, y0, y1; } ap[2] = {
        { dm, -1.0f, land, -lt, 0.0f }, { am, c->len - land, 9.0f, c->rise - lt, c->rise },
    };
    for (int k = 0; k < 2; k++) {
        if (!ap[k].m)   continue;

        struct sink_walk aw = walk_one(ap[k].m);
        double vol = ((double)ap[k].a1 - ap[k].a0) * deep * lt;
        if (mesh_nr_vx(ap[k].m) != 36 || !walk_closed(&aw))                                 return false;
        if (fabs(walk_volume(&aw) - vol) > 1e-4 * vol)                                      return false;
        if (!walk_aabb_is(&aw, &c->frame, (vec3){ ap[k].a0, ap[k].y0, 0.0f }, (vec3){ ap[k].a1, ap[k].y1, deep }))
            return false;
    }

    size_t nr_vx = sloped * balustrade_nr_vx(&p->rail, run, c->rise);
    float lens[2] = { land - (-1.0f + p->rim_clear), 9.0f - p->rim_clear - (c->len - land) };
    for (int k = 0; k < landings; k++)  nr_vx += balustrade_nr_vx(&p->rail, lens[k], 0.0f);

    return mesh_nr_vx(rail) == nr_vx && walk_closed(&w);
}

static int flight_test(void)
{
    struct flight_case c;
    flight_case_init(&c, &PCG_FRAME_IDENTITY);

    /* the defaults emit, the aprons solid floor and structure */
    if (c.p.depart.role != PCG_ROLE_FLOOR || c.p.depart.collide != PCG_COLLIDE_SOLID)       return EXIT_FAILURE;
    if (c.p.arrive.role != PCG_ROLE_STRUCTURE || c.p.arrive.collide != PCG_COLLIDE_SOLID)   return EXIT_FAILURE;
    if (CRES_RET(pcg_stair_steps(&c.p.stair, c.rise), return EXIT_FAILURE) != 18)           return EXIT_FAILURE;
    if (!flight_case_ok(&c, true, true, 2, 2))                                              return EXIT_FAILURE;
    if (!element_frames_ok(flight_case_emit, &c, &rail_palette, true))                      return EXIT_FAILURE;

    /* placed in its parent, yawed or mirrored, with the voids placed alike: the same flight in its frame */
    for (int i = 0; i < 2; i++) {
        pcg_frame f;
        struct flight_case m;

        test_frame(&f, 0.7f + i, i, 3.0f, 1.5f, -2.0f);
        flight_case_init(&m, &f);
        if (!flight_case_ok(&m, true, true, 2, 2))  return EXIT_FAILURE;
    }

    /* without voids, the flight and its sloped rails alone; with one, its apron and landing rail */
    struct flight_case e = c;
    e.nr_depart = e.nr_arrive = 0;
    if (!flight_case_ok(&e, false, false, 2, 0))    return EXIT_FAILURE;
    e.nr_arrive = 4;
    if (!flight_case_ok(&e, false, true, 2, 1)) return EXIT_FAILURE;

    /* without the near rail; with the near rail alone */
    e = c;
    e.p.rail_near = false;
    if (!flight_case_ok(&e, true, true, 1, 2))  return EXIT_FAILURE;
    e.p.rail_near = true;
    e.p.rail_far = false;
    if (!flight_case_ok(&e, true, true, 1, 0))  return EXIT_FAILURE;

    /* a void that leaves a sliver beside the flight leaves no apron; a skipped apron is gone */
    e = c;
    flight_void(&e.frame, -1.0f, 9.0f, 1.2f, 4.0f, e.depart);
    if (!flight_case_ok(&e, false, true, 2, 2)) return EXIT_FAILURE;
    e = c;
    e.p.depart.skip = true;
    if (!flight_case_ok(&e, false, true, 2, 2)) return EXIT_FAILURE;

    /* zeroed or nonsense style, a chord too short for the landings, voids missing or short */
    LOCAL_SET(pcg_sink, bad) = CRES_RET(pcg_sink_new("flight"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bad, .palette = &rail_palette, .frame = PCG_FRAME_IDENTITY };

    struct flight_case z = c;
    z.p = (pcg_flight_params){};
    if (!failed(flight_case_emit(&bc, &z)))     return EXIT_FAILURE;
    float *neg[] = { &z.p.gap, &z.p.rim_clear, &z.p.min_landing, &z.p.min_apron, &z.p.air };
    for (size_t k = 0; k < array_size(neg); k++) {
        z = c;
        *neg[k] = -0.01f;
        if (!failed(flight_case_emit(&bc, &z)))     return EXIT_FAILURE;
    }
    /* a child's style fails the whole flight too, before anything is emitted */
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = {
            &z.p.gap, &z.p.rail_inset, &z.p.rim_clear, &z.p.min_landing, &z.p.min_apron, &z.p.air, &z.len,
            &z.rise, &z.depart[2][0], &z.arrive[1][1], &z.p.stair.waist, &z.p.stair.riser_max, &z.p.rail.h,
            &z.p.rail.slat_w,
        };

        for (size_t k = 0; k < array_size(fields); k++) {
            z = c;
            *fields[k] = nonfinite[i];
            if (!failed_inval(flight_case_emit(&bc, &z)))   return EXIT_FAILURE;
        }
    }
    for (size_t i = 0; i < array_size(bad_frames); i++) {
        z = c;
        z.frame = bad_frames[i];
        if (!failed_inval(flight_case_emit(&bc, &z)))   return EXIT_FAILURE;
    }
    /* a void with a notch, a void crossing over itself, a void without an area: none of them convex */
    const float bad_void[3][4][2] = {
        { { -1.0f, 0.0f }, { 9.0f, 0.0f }, { 9.0f, 4.0f }, { 4.0f, 1.0f } },
        { { -1.0f, 0.0f }, { 9.0f, 4.0f }, { 9.0f, 0.0f }, { -1.0f, 4.0f } },
        { { -1.0f, 0.0f }, { 2.0f, 0.0f }, { 5.0f, 0.0f }, { 9.0f, 0.0f } },
    };
    for (size_t i = 0; i < array_size(bad_void); i++) {
        z = c;
        for (int k = 0; k < 4; k++) {
            vec3 p;

            pcg_frame_point(&z.frame, (vec3){ bad_void[i][k][0], 0.0f, bad_void[i][k][1] }, p);
            z.arrive[k][0] = p[0];
            z.arrive[k][1] = p[2];
        }
        if (!failed_inval(flight_case_emit(&bc, &z)))   return EXIT_FAILURE;
    }
    z = c;
    z.len = 0.0f;
    if (!failed(flight_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z.len = 6.0f;       /* 0.48 m landings */
    if (!failed(flight_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.nr_depart = 2;
    if (!failed(flight_case_emit(&bc, &z)))     return EXIT_FAILURE;
    pcg_flight_site s;
    flight_case_site(&c, &s);
    s.arrive_void = nullptr;
    if (!failed(_pcg_flight_emit(&bc, &c.p, &s)))   return EXIT_FAILURE;
    if (pcg_sink_get_stats(bad).nr_models)          return EXIT_FAILURE;

    /* the check takes the flight the emit takes and turns down what it turns down */
    flight_case_site(&c, &s);
    CERR_RET(pcg_flight_check(&c.p, &s), return EXIT_FAILURE);
    s.len = 6.0f;
    if (!failed_inval(pcg_flight_check(&c.p, &s)))                          return EXIT_FAILURE;
    flight_case_site(&c, &s);
    if (!failed_inval(pcg_flight_check(&(pcg_flight_params){}, &s)))        return EXIT_FAILURE;
    if (!failed_inval(pcg_flight_check(&c.p, nullptr)))                     return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* the same frame, to the bit */
static bool frame_eq(const pcg_frame *a, const pcg_frame *b)
{
    return a->org[0] == b->org[0] && a->org[1] == b->org[1] && a->org[2] == b->org[2] &&
           a->dir[0] == b->dir[0] && a->dir[1] == b->dir[1] && a->mirror == b->mirror;
}

static int flight_cut_test(void)
{
    struct flight_case c;
    pcg_frame f;
    test_frame(&f, 0.4f, true, 2.0f, 6.0f, -5.0f);
    flight_case_init(&c, &f);

    pcg_flight_site s;
    flight_case_site(&c, &s);
    const pcg_flight_params *p = &c.p;
    int steps = 18;
    float run = steps * p->stair.going, land = (c.len - run) * 0.5f, H = c.rise;

    /* departing: open behind, up to where the soffit clears a 1.05 rail; out to the far edge */
    pcg_region r;
    CERR_RET(pcg_flight_depart_cut(p, &s, 1.05f, &r), return EXIT_FAILURE);
    float hi = land + run * (1.05f + p->stair.waist + H / steps + p->air) / H;
    if (!r.along.open_lo || r.along.open_hi || !near(r.along.hi, hi, 1e-6f))                return EXIT_FAILURE;
    if (!r.across.open_lo || r.across.open_hi || !near(r.across.hi, p->gap + p->stair.width, 1e-6f))
        return EXIT_FAILURE;
    if (!frame_eq(&r.frame, &s.frame) || failed(pcg_regions_check(&r, 1)))  return EXIT_FAILURE;

    /* the rim along the chord, from 1 m behind its start, is cut up to there */
    vec3 a, b;
    pcg_span cut;
    pcg_frame_point(&f, (vec3){ -1.0f, 0.0f, 0.0f }, a);
    pcg_frame_point(&f, (vec3){ 9.0f, 0.0f, 0.0f }, b);
    if (!pcg_region_cut(&r, (vec2){ a[0], a[2] }, (vec2){ b[0], b[2] }, 1e-3f, &cut))       return EXIT_FAILURE;
    if (!near(cut.lo, 0.0f, 1e-4f) || !near(cut.hi, hi + 1.0f, 1e-4f))                      return EXIT_FAILURE;

    /* arriving: from where the treads are the headroom and the hang under the floor, on past the landing */
    CERR_RET(pcg_flight_arrive_cut(p, &s, 2.0f, 0.5f, &r), return EXIT_FAILURE);
    float lo = c.len - land - (2.0f + 0.5f + p->air) / (H / steps / p->stair.going);
    if (r.along.open_lo || !r.along.open_hi || !near(r.along.lo, lo, 1e-6f))                return EXIT_FAILURE;
    if (!r.across.open_lo || r.across.open_hi || !near(r.across.hi, p->gap + p->stair.width, 1e-6f))
        return EXIT_FAILURE;
    if (!frame_eq(&r.frame, &s.frame) || failed(pcg_regions_check(&r, 1)))                  return EXIT_FAILURE;
    if (!pcg_region_cut(&r, (vec2){ a[0], a[2] }, (vec2){ b[0], b[2] }, 1e-3f, &cut))       return EXIT_FAILURE;
    if (!near(cut.lo, lo + 1.0f, 1e-4f) || !near(cut.hi, 10.0f, 1e-4f))                     return EXIT_FAILURE;

    /* heights that are negative or aren't finite, and what the flight turns down */
    const float bad[] = { -0.1f, INFINITY, NAN };
    for (size_t i = 0; i < array_size(bad); i++) {
        if (!failed(pcg_flight_depart_cut(p, &s, bad[i], &r)))              return EXIT_FAILURE;
        if (!failed(pcg_flight_arrive_cut(p, &s, bad[i], 0.0f, &r)))        return EXIT_FAILURE;
        if (!failed(pcg_flight_arrive_cut(p, &s, 2.0f, bad[i], &r)))        return EXIT_FAILURE;
    }
    if (!failed(pcg_flight_depart_cut(p, &s, 1.0f, nullptr)))               return EXIT_FAILURE;
    if (!failed(pcg_flight_arrive_cut(nullptr, &s, 1.0f, 0.0f, &r)))        return EXIT_FAILURE;
    s.len = 6.0f;
    if (!failed(pcg_flight_depart_cut(p, &s, 1.0f, &r)))                    return EXIT_FAILURE;
    s.len = c.len;
    struct flight_case z = c;
    z.p.stair.going = NAN;
    if (!failed(pcg_flight_arrive_cut(&z.p, &s, 2.0f, 0.0f, &r)))           return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* reference copies of the polygon clip and the apron emitter the flight's aprons replace */
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

static cerr ref_apron_emit(struct mesh *m, int n, float r, const ref_flight *g, float al0, float al1, float acmax,
                           float min_depth, float y0, float y1, float uv)
{
    vec2 buf[2][REF_WELL_SIDES + 8];
    int np = n;

    for (int j = 0; j < np; j++)    ref_ngon_vertex(j, n, r, buf[0][j]);

    float dA = g->w.A[0] * g->w.dir[0] + g->w.A[1] * g->w.dir[1];
    float aA = g->w.A[0] * g->w.in[0] + g->w.A[1] * g->w.in[1];

    np = ref_poly_clip(buf[0], np, (vec2){ -g->w.dir[0], -g->w.dir[1] }, -(dA + al0), buf[1]);
    np = ref_poly_clip(buf[1], np, g->w.dir, dA + al1, buf[0]);
    np = ref_poly_clip(buf[0], np, g->w.in, aA + acmax, buf[1]);

    if (np < 3) return CERR_OK;

    if (min_depth > 0.0f) {
        float mn = 0.0f, mx = 0.0f;

        for (int j = 0; j < np; j++) {
            float ac = buf[1][j][0] * g->w.in[0] + buf[1][j][1] * g->w.in[1] - aA;

            if (!j) { mn = mx = ac; continue; }
            mn = fminf(mn, ac);
            mx = fmaxf(mx, ac);
        }
        if (mx - mn < min_depth)    return CERR_OK;
    }

    return ref_convex_plate_emit(m, buf[1], np, y0, y1, uv);
}

/* @src's triangles moved up to @y, in a mesh of their own */
static cresp(mesh) ref_placed(struct mesh *src, float y)
{
    LOCAL_SET(mesh_t, m) = CRES_RET_T(ref_new_checked(mesh, .name = "ref placed"), mesh);

    CERR_RET_T(ref_instance(m, src, (vec3){ 0.0f, y, 0.0f }), mesh);

    return cresp_val(mesh, NOCU(m));
}

/* the cut @r is the reference window @w: the same frame on the ground and the same ranges */
static bool flight_cut_is(const pcg_region *r, const ref_window *w)
{
    pcg_region e;
    ref_window_region(w, &e);

    if (r->frame.org[0] != e.frame.org[0] || r->frame.org[2] != e.frame.org[2] || r->frame.mirror)      return false;
    if (!near(r->frame.dir[0], e.frame.dir[0], 1e-6f) || !near(r->frame.dir[1], e.frame.dir[1], 1e-6f)) return false;

    const pcg_range *rr[2] = { &r->along, &r->across }, *er[2] = { &e.along, &e.across };
    for (int k = 0; k < 2; k++) {
        if (rr[k]->open_lo != er[k]->open_lo || rr[k]->open_hi != er[k]->open_hi)   return false;
        if (!rr[k]->open_lo && !near(rr[k]->lo, er[k]->lo, 1e-5f))                  return false;
        if (!rr[k]->open_hi && !near(rr[k]->hi, er[k]->hi, 1e-5f))                  return false;
    }

    return true;
}

/*
 * The reference building's three flights at @frame, their rails and
 * aprons included, against reference copies of the emitters, called the
 * way the reference building calls them. Flight f climbs a 6 m storey from
 * the 0.05 deck of level f, along the chord from vertex 2f of level f's
 * well, level 1's for the ground flight, to vertex 2f + 1 of level
 * f + 1's, in 50 risers of 0.12 and 0.20 goings, 5 m wide, 0.03 off the
 * chord, on a 0.22 waist. Its aprons are 0.28 thick, the waist and 0.06,
 * clipped to the wells and kept if they reach 0.05 across: the arrival
 * apron always, the departure apron only above the ground floor, whose
 * deck is solid. The rails stand 0.06 and 5.06 off the chord, the landing
 * rails 0.06 past the rim. The sloped rails' shoe and cap are checked for
 * shape alone: their charts and tangents are the accepted change. The
 * boarding windows are the reference building's: a 1.05 rail clear of the
 * soffit, a 3.2 m walker clear of the deck, and of the 0.9 fascia under
 * it, with 0.1 of air.
 */
static int flight_parity_frame(const pcg_frame *frame)
{
    const int n = REF_WELL_SIDES;
    const float H = REF_FLOOR_H, sw = REF_STAIR_W, lt = 0.22f + 0.06f;
    const float deep = 0.03f + sw, crail = deep + 0.03f;
    pcg_flight_params p;

    pcg_flight_params_init(&p);
    p.stair.width = sw;
    p.stair.riser_max = 0.12f;
    p.stair.going = REF_GOING;
    p.stair.waist = 0.22f;
    p.stair.landing_t = lt;
    p.arrive.mat = &apron;
    p.rail.cap.mat = p.rail.shoe.mat = &rod;

    for (int f = 0; f < REF_FLOORS - 1; f++) {
        float y = f * H + REF_DECK_T;
        vec2 dv[REF_WELL_SIDES], av[REF_WELL_SIDES], B;
        ref_flight g;

        ref_flight_calc(&g, ref_well_r, n, f, REF_STEPS, REF_GOING);
        for (int j = 0; j < n; j++) {
            ref_ngon_vertex(j, n, ref_well_r[(f ? f : 1) - 1], dv[j]);
            ref_ngon_vertex(j, n, ref_well_r[f], av[j]);
        }
        ref_ngon_vertex(2 * f + 1, n, ref_well_r[f], B);

        pcg_flight_site site = {
            .rise           = H,
            .depart_void    = f ? dv : nullptr,
            .nr_depart_void = f ? n : 0,
            .arrive_void    = av,
            .nr_arrive_void = n,
        };
        site.len = CRES_RET(pcg_frame_run(&site.frame, g.w.A, B, y), return EXIT_FAILURE);
        if (!near(site.len, g.len, 1e-5f))  return EXIT_FAILURE;

        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("flight"), return EXIT_FAILURE);
        pcg_ctx ctx = { .sink = sink, .palette = &rail_palette, .frame = *frame };
        CERR_RET(_pcg_flight_emit(&ctx, &p, &site), return EXIT_FAILURE);

        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != (f ? 5u : 4u) || (!f && walk_mesh(&w, &wood)))  return EXIT_FAILURE;

        /* the stair and the aprons */
        ref_stair_params rp = {
            .org = { g.w.A[0] + g.w.dir[0] * g.land + g.w.in[0] * (0.03f + sw * 0.5f),
                     g.w.A[1] + g.w.dir[1] * g.land + g.w.in[1] * (0.03f + sw * 0.5f) },
            .dir = { g.w.dir[0], g.w.dir[1] },
            .width = sw, .rise = H, .steps = REF_STEPS,
            .going = REF_GOING, .waist = 0.22f,
            .uv = REF_UV_CONCRETE,
        };
        LOCAL_SET(mesh_t, rs) = CRES_RET(ref_new_checked(mesh, .name = "ref stair"), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, ra) = CRES_RET(ref_new_checked(mesh, .name = "ref arrival"), return EXIT_FAILURE);
        if (failed(ref_stair_emit(rs, &rp)))    return EXIT_FAILURE;
        if (failed(ref_apron_emit(ra, n, ref_well_r[f], &g, g.len - g.land, 1e9f, deep, 0.05f, H - lt, H,
                                  REF_UV_CONCRETE)))
            return EXIT_FAILURE;
        if (f) {
            LOCAL_SET(mesh_t, rd) = CRES_RET(ref_new_checked(mesh, .name = "ref departure"), return EXIT_FAILURE);
            if (failed(ref_apron_emit(rd, n, ref_well_r[f - 1], &g, -1e9f, g.land, deep, 0.05f, -lt, 0.0f,
                                      REF_UV_FLOOR)))
                return EXIT_FAILURE;

            LOCAL_SET(mesh_t, pd) = CRES_RET(ref_placed(rd, y), return EXIT_FAILURE);
            if (!parity_check("flight departure", walk_mesh(&w, &wood), pd, frame, true, REF_UV_FLOOR))
                return EXIT_FAILURE;
        }

        /* the rails: the sloped pair, rim side then well side, then the landings' level rails */
        LOCAL_SET(mesh_t, lm) = CRES_RET(ref_new_checked(mesh, .name = "ref slats"), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, lr) = CRES_RET(ref_new_checked(mesh, .name = "ref shoes and caps"), return EXIT_FAILURE);
        ref_balustrade_params bp;
        ref_rail_init(&bp, g.w.dir);
        bp.boxes = lr;

#define FL_ORG(_a, _c) do { \
    bp.org[0] = g.w.A[0] + g.w.dir[0] * (_a) + g.w.in[0] * (_c); \
    bp.org[1] = g.w.A[1] + g.w.dir[1] * (_a) + g.w.in[1] * (_c); \
} while (0)
        bp.len = g.run;
        bp.rise = H;
        FL_ORG(g.land, 0.03f + 0.03f);
        if (failed(ref_balustrade_emit(lm, lr, &bp)))   return EXIT_FAILURE;
        FL_ORG(g.land, crail);
        if (failed(ref_balustrade_emit(lm, lr, &bp)))   return EXIT_FAILURE;

        float b0, b1;
        int nr_landings = 0;
        bp.rise = 0.0f;
        if (f && ref_poly_span_frame(n, ref_well_r[f - 1], &g, crail, &b0, &b1) > 0.0f) {
            bp.len = g.land - (b0 + 0.06f);
            if (bp.len > 0.15f) {
                FL_ORG(b0 + 0.06f, crail);
                if (failed(ref_balustrade_emit(lm, lr, &bp)))   return EXIT_FAILURE;
                nr_landings++;
            }
        }
        if (ref_poly_span_frame(n, ref_well_r[f], &g, crail, &b0, &b1) > 0.0f) {
            bp.y0 = H;
            bp.len = b1 - 0.06f - (g.len - g.land);
            if (bp.len > 0.15f) {
                FL_ORG(g.len - g.land, crail);
                if (failed(ref_balustrade_emit(lm, lr, &bp)))   return EXIT_FAILURE;
                nr_landings++;
            }
        }
#undef FL_ORG
        /* landing rails were there to compare */
        if (nr_landings != (f ? 2 : 1))     return EXIT_FAILURE;

        LOCAL_SET(mesh_t, ps) = CRES_RET(ref_placed(rs, y), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, pa) = CRES_RET(ref_placed(ra, y), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, pm) = CRES_RET(ref_placed(lm, y), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, pr) = CRES_RET(ref_placed(lr, y), return EXIT_FAILURE);
        if (!parity_check("flight stair", walk_mesh(&w, &concrete), ps, frame, false, REF_UV_CONCRETE))
            return EXIT_FAILURE;
        if (!parity_check("flight arrival", walk_mesh(&w, &apron), pa, frame, true, REF_UV_CONCRETE))
            return EXIT_FAILURE;
        if (!parity_check("flight slats", walk_mesh(&w, &slat), pm, frame, false, REF_UV_SLAT)) return EXIT_FAILURE;
        if (!parity_check_shape("flight shoes and caps", walk_mesh(&w, &rod), pr, frame))       return EXIT_FAILURE;

        /* the boarding windows */
        float slope = H / REF_STEPS / REF_GOING;
        ref_window wd = g.w, wa = g.w, wf = g.w;
        wd.al0 = -1e9f;
        wd.al1 = g.land + g.run * (REF_RAIL_H + 0.22f + H / REF_STEPS + 0.1f) / H;
        wa.al0 = g.len - g.land - (REF_AGENT_H + 0.1f) / slope;
        wf.al0 = g.len - g.land - (REF_AGENT_H + 0.9f + REF_DECK_T + 0.1f) / slope;
        wa.al1 = wf.al1 = 1e9f;
        wd.acmax = wa.acmax = wf.acmax = 0.03f + REF_STAIR_W;

        pcg_region dc, ac, fc;
        CERR_RET(pcg_flight_depart_cut(&p, &site, p.rail.h, &dc), return EXIT_FAILURE);
        CERR_RET(pcg_flight_arrive_cut(&p, &site, REF_AGENT_H, 0.0f, &ac), return EXIT_FAILURE);
        CERR_RET(pcg_flight_arrive_cut(&p, &site, REF_AGENT_H, 0.9f + REF_DECK_T, &fc), return EXIT_FAILURE);
        if (!flight_cut_is(&dc, &wd) || !flight_cut_is(&ac, &wa) || !flight_cut_is(&fc, &wf))   return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int flight_parity_test(void)
{
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    for (int fi = 0; fi < 2; fi++)
        if (flight_parity_frame(&frames[fi]))   return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Gallery
 ****************************************************************************/

/* the fascia's own finish, so that its box charts compare apart from the slab's plate charts */
static const pcg_material band = { .name = "band", .uv_mode = PCG_UV_METERS };

/* a gallery level with up to one cut each for its rail and its fascia */
struct gallery_case {
    pcg_gallery_params  p;
    pcg_frame           frame;
    vec2                outline[6];
    vec2                rim[6];
    size_t              nr_rim;
    pcg_region          rail_cut;
    size_t              nr_rail_cuts;
    pcg_region          fascia_cut;
    size_t              nr_fascia_cuts;
};

/* the window [@a0, @a1] by [-1, 1] across the rim's edge @i */
static void gallery_cut(const struct gallery_case *c, size_t i, float a0, float a1, pcg_region *r)
{
    vec2 dir, left;
    const float *a = c->rim[i];

    poly2d_edge_frame(a, c->rim[(i + 1) % c->nr_rim], dir, left);
    *r = (pcg_region){
        .frame  = { .org = { a[0], 0.0f, a[1] }, .dir = { dir[0], dir[1] } },
        .along  = { .lo = a0, .hi = a1 },
        .across = { .lo = -1.0f, .hi = 1.0f },
    };
}

/*
 * A hexagonal floor of 12 around a hexagonal well of 5, raised and moved
 * off its parent's origin; the rail broken over [1, 2.5] of the rim's
 * second edge, the fascia over [2, 3]
 */
static void gallery_case_init(struct gallery_case *c)
{
    pcg_gallery_params_init(&c->p);
    c->frame = (pcg_frame){ .org = { 1.0f, 4.0f, -2.0f }, .dir = { 1.0f, 0.0f } };
    for (int j = 0; j < 6; j++) {
        ref_ngon_vertex(j, 6, 12.0f, c->outline[j]);
        ref_ngon_vertex(j, 6, 5.0f, c->rim[j]);
    }
    c->nr_rim = 6;
    gallery_cut(c, 1, 1.0f, 2.5f, &c->rail_cut);
    gallery_cut(c, 1, 2.0f, 3.0f, &c->fascia_cut);
    c->nr_rail_cuts = c->nr_fascia_cuts = 1;
}

static cerr gallery_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct gallery_case *c = priv;

    return pcg_gallery_emit(ctx, &c->p, .frame = c->frame, .outline = c->outline, .nr_outline = 6,
                            .rim = c->rim, .nr_rim = c->nr_rim,
                            .rail_cuts = c->nr_rail_cuts ? &c->rail_cut : nullptr, .nr_rail_cuts = c->nr_rail_cuts,
                            .fascia_cuts = c->nr_fascia_cuts ? &c->fascia_cut : nullptr,
                            .nr_fascia_cuts = c->nr_fascia_cuts);
}

/*
 * @c's level: the slab's 12 cap triangles a face and an edge per ring
 * edge, on both the structure and the finish; a fascia run and a rail run
 * on every rim edge, each cut where its own cut crosses; and the volume
 * of all of it, every member counted whole
 */
static bool gallery_case_ok(const struct gallery_case *c, size_t *nr_fascia, size_t *nr_rail)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(gallery_case_emit, c, &rail_palette, &PCG_FRAME_IDENTITY),
                                         return false);
    struct sink_walk w;
    if (!sink_walk_get(sink, &w) || w.nr != 3)  return false;

    struct fascia_case fc = { .p = c->p.fascia, .nr_pts = c->nr_rim, .nr_cuts = c->nr_fascia_cuts, .closed = true };
    struct balustrade_case bc = {
        .p = c->p.rail, .nr_pts = c->nr_rim, .nr_cuts = c->nr_rail_cuts, .inset = c->p.rail_inset, .closed = true,
    };
    memcpy(fc.pts, c->rim, sizeof(c->rim));
    memcpy(bc.pts, c->rim, sizeof(c->rim));
    fc.cuts[0] = c->fascia_cut;
    bc.cuts[0] = c->rail_cut;

    double fascia_len, rail_vol;
    *nr_fascia = fascia_case_runs(&fc, &fascia_len);
    *nr_rail = balustrade_case_nr_vx(&bc, &rail_vol);

    const pcg_slab_params *s = &c->p.floor;
    size_t nr_slab = 12 * 3 + 12 * 6;
    struct mesh *structure = walk_mesh(&w, &concrete), *deck = walk_mesh(&w, &wood), *rail = walk_mesh(&w, &slat);
    if (!structure || mesh_nr_vx(structure) != nr_slab + *nr_fascia * 36)       return false;
    if (!deck || mesh_nr_vx(deck) != nr_slab)                                   return false;
    if (!rail || mesh_nr_vx(rail) != *nr_rail)                                  return false;
    if (!walk_closed(&w))                                                       return false;

    double vol = (ngon_area(6, 12.0) - ngon_area(6, 5.0)) * ((double)s->t + s->finish_t) +
                 fascia_len * c->p.fascia.depth * ((double)c->p.fascia.lip + c->p.fascia.tuck) + rail_vol;
    if (fabs(walk_volume(&w) - vol) > 1e-4 * vol)                               return false;

    /* from under the fascia to the top of the rail, out to the outline */
    vec2 lo, hi;
    poly2d_bbox(c->outline, 6, lo, hi);

    return walk_aabb_is(&w, &c->frame, (vec3){ lo[0], -c->p.fascia.depth, lo[1] },
                        (vec3){ hi[0], s->finish_t + c->p.rail.h, hi[1] });
}

static int gallery_test(void)
{
    struct gallery_case c;
    gallery_case_init(&c);

    /* the children's defaults, a thicker slab and the rail back from the rim */
    pcg_slab_params sp;
    pcg_fascia_params fp;
    pcg_balustrade_params bp;
    pcg_slab_params_init(&sp);
    pcg_fascia_params_init(&fp);
    pcg_balustrade_params_init(&bp);
    if (c.p.floor.t != 0.35f || c.p.floor.finish_t != sp.finish_t || c.p.rail_inset != 0.09f)   return EXIT_FAILURE;
    if (c.p.fascia.depth != fp.depth || c.p.fascia.lip != fp.lip || c.p.rail.h != bp.h)         return EXIT_FAILURE;
    for (int s = 0; s < PCG_SLAB_NR_SURF; s++)
        if (c.p.floor.surf[s].role != sp.surf[s].role || c.p.floor.surf[s].collide != sp.surf[s].collide)
            return EXIT_FAILURE;

    /* each cut breaks its own child: one more run of the fascia, one more of the rail */
    size_t nr_fascia, nr_rail, nr_fascia_whole, nr_rail_whole;
    if (!gallery_case_ok(&c, &nr_fascia, &nr_rail) || nr_fascia != 7)   return EXIT_FAILURE;
    if (!element_frames_ok(gallery_case_emit, &c, &rail_palette, true)) return EXIT_FAILURE;

    struct gallery_case e = c;
    e.nr_rail_cuts = e.nr_fascia_cuts = 0;
    if (!gallery_case_ok(&e, &nr_fascia_whole, &nr_rail_whole)) return EXIT_FAILURE;
    if (nr_fascia_whole != 6 || !(nr_rail_whole > nr_rail))     return EXIT_FAILURE;
    e.nr_rail_cuts = 1;
    if (!gallery_case_ok(&e, &nr_fascia_whole, &nr_rail_whole)) return EXIT_FAILURE;
    if (nr_fascia_whole != 6 || nr_rail_whole != nr_rail)       return EXIT_FAILURE;

    /* a level without a rail */
    e = c;
    for (int s = 0; s < PCG_BALUSTRADE_NR_SURF; s++)    e.p.rail.surf[s].skip = true;
    LOCAL_SET(pcg_sink, ns) = CRES_RET(element_sink(gallery_case_emit, &e, &rail_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(ns, &w) || w.nr != 2 || walk_mesh(&w, &slat))    return EXIT_FAILURE;

    /* zeroed or nonsense style, short or broken rings, cuts missing or not finite: nothing emitted */
    LOCAL_SET(pcg_sink, bad) = CRES_RET(pcg_sink_new("gallery"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bad, .palette = &rail_palette, .frame = PCG_FRAME_IDENTITY };

    struct gallery_case z = c;
    z.p = (pcg_gallery_params){};
    if (!failed(gallery_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.p.fascia.depth = 0.0f;
    if (!failed(gallery_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.p.rail.cap_t = z.p.rail.h;
    if (!failed(gallery_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.p.floor.t = 0.0f;
    if (!failed(gallery_case_emit(&bc, &z)))    return EXIT_FAILURE;
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = {
            &z.p.rail_inset, &z.p.floor.t, &z.p.floor.finish_t, &z.p.fascia.depth, &z.p.fascia.lip,
            &z.p.rail.h, &z.p.rail.gap, &z.outline[2][0], &z.rim[4][1], &z.rail_cut.along.hi,
            &z.fascia_cut.across.lo,
        };

        for (size_t k = 0; k < array_size(fields); k++) {
            z = c;
            *fields[k] = nonfinite[i];
            if (!failed_inval(gallery_case_emit(&bc, &z)))  return EXIT_FAILURE;
        }
    }
    for (size_t i = 0; i < array_size(bad_frames); i++) {
        z = c;
        z.frame = bad_frames[i];
        if (!failed_inval(gallery_case_emit(&bc, &z)))  return EXIT_FAILURE;
    }
    z = c;
    z.nr_rim = 2;
    if (!failed(gallery_case_emit(&bc, &z)))    return EXIT_FAILURE;
    /* a rim wound the other way round would stand the rail over the void */
    z = c;
    for (int j = 0; j < 6; j++)                     memcpy(z.rim[j], c.rim[5 - j], sizeof(vec2));
    if (!failed_inval(gallery_case_emit(&bc, &z)))  return EXIT_FAILURE;
    z = c;
    memcpy(z.rim[3], z.rim[2], sizeof(vec2));
    if (!failed(gallery_case_emit(&bc, &z)))    return EXIT_FAILURE;
    if (!failed(pcg_gallery_emit(&bc, &c.p, .frame = c.frame, .outline = c.outline, .nr_outline = 6,
                                 .rim = c.rim, .nr_rim = 6, .nr_rail_cuts = 1)))
        return EXIT_FAILURE;
    if (!failed(pcg_gallery_emit(&bc, &c.p, .frame = c.frame, .outline = c.outline, .nr_outline = 6,
                                 .rim = c.rim, .nr_rim = 6, .nr_fascia_cuts = 1)))
        return EXIT_FAILURE;
    if (!failed(pcg_gallery_emit(&bc, &c.p, .frame = c.frame, .nr_outline = 6, .rim = c.rim, .nr_rim = 6)))
        return EXIT_FAILURE;
    if (pcg_sink_get_stats(bad).nr_models)      return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* a gallery level's params and site */
struct gallery_level {
    const pcg_gallery_params    *p;
    pcg_gallery_site            site;
};

static cerr gallery_level_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct gallery_level *l = priv;

    return _pcg_gallery_emit(ctx, l->p, &l->site);
}

/*
 * The reference building's gallery levels 1 to 3 at @frame, against
 * reference copies of the emitters, as the reference building builds a
 * level of 6 m storeys: a 0.35 slab without its top under a 0.05 deck
 * without its bottom, a hexagonal floor of 60 around the level's well of
 * 20, 24 or 28; the 0.9 fascia under the rim, broken over the arriving
 * flight's window; and the rail 0.09 back from the rim on the deck,
 * broken over the windows of the arriving and the departing flight
 * (balustrade_parity_rim() and fascia_parity_test() build the same
 * windows). The fascia wears a finish of its own here, so that its box
 * charts compare apart from the slab's plate charts.
 */
static int gallery_parity_frame(const pcg_frame *frame)
{
    const int n = REF_WELL_SIDES, nr_flights = REF_FLOORS - 1;
    const float H = REF_FLOOR_H;
    pcg_gallery_params p;

    pcg_gallery_params_init(&p);
    p.floor.finish_t = REF_DECK_T;
    p.fascia.face.mat = p.fascia.back.mat = &band;

    for (int f = 1; f < REF_FLOORS; f++) {
        float rf = ref_well_r[f - 1], slope = H / REF_STEPS / REF_GOING;
        ref_window win[2], fwin = {};
        pcg_region rail_cuts[2], fascia_cut;
        int nr_win = 0;

        for (int e = 0; e < 2; e++) {
            int fl = f - 1 + e;
            ref_flight g;

            if (fl >= nr_flights)   continue;
            ref_flight_calc(&g, ref_well_r, n, fl, REF_STEPS, REF_GOING);
            win[nr_win] = g.w;

            if (e) {
                win[nr_win].al0 = -1e9f;
                win[nr_win].al1 = g.land + g.run * (REF_RAIL_H + 0.22f + H / REF_STEPS + 0.1f) / H;
            } else {
                win[nr_win].al0 = g.len - g.land - (REF_AGENT_H + 0.1f) / slope;
                win[nr_win].al1 = 1e9f;

                fwin = g.w;
                fwin.al0 = g.len - g.land - (REF_AGENT_H + 0.9f + REF_DECK_T + 0.1f) / slope;
                fwin.al1 = 1e9f;
                fwin.acmax = 0.03f + REF_STAIR_W;
                ref_window_region(&fwin, &fascia_cut);
            }
            win[nr_win].acmax = 0.03f + REF_STAIR_W;
            ref_window_region(&win[nr_win], &rail_cuts[nr_win]);
            nr_win++;
        }

        /*
         * The original takes the slab hole's points at angles a turn below
         * the rim's, which a float rounds apart, by up to 1.3e-5 at 28 m:
         * the slab and the deck compare around a rim of the hole's points,
         * the fascia and the rail around the rim's own. The hole runs the
         * other way, which the triangulation doesn't mind.
         */
        vec2 outline[REF_WELL_SIDES], rims[2][REF_WELL_SIDES];
        for (int j = 0; j < n; j++) {
            ref_ngon_vertex(j, 6, 60.0f, outline[j]);
            ref_ngon_vertex(j, n, rf, rims[0][j]);
            ref_ngon_vertex(j ? j - n : 0, n, rf, rims[1][j]);
        }

        struct gallery_level lv = {
            .p      = &p,
            .site   = {
                .frame          = { .org = { 0.0f, f * H, 0.0f }, .dir = { 1.0f, 0.0f } },
                .outline        = outline,
                .nr_outline     = 6,
                .rim            = rims[0],
                .nr_rim         = n,
                .rail_cuts      = rail_cuts,
                .nr_rail_cuts   = nr_win,
                .fascia_cuts    = &fascia_cut,
                .nr_fascia_cuts = 1,
            },
        };
        LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(gallery_level_emit, &lv, &rail_palette, frame),
                                             return EXIT_FAILURE);
        lv.site.rim = rims[1];
        LOCAL_SET(pcg_sink, hs) = CRES_RET(element_sink(gallery_level_emit, &lv, &rail_palette, frame),
                                           return EXIT_FAILURE);

        struct sink_walk w, hw;
        if (!sink_walk_get(sink, &w) || !sink_walk_get(hs, &hw) || w.nr != 4 || hw.nr != 4)         return EXIT_FAILURE;
        for (size_t b = 0; b < w.nr; b++)
            if (w.key[b].mat != hw.key[b].mat || mesh_nr_vx(w.mesh[b]) != mesh_nr_vx(hw.mesh[b]))   return EXIT_FAILURE;

        float len;
        LOCAL_SET(mesh_t, rs) = CRES_RET(ref_new_checked(mesh, .name = "ref slab"), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, rd) = CRES_RET(ref_new_checked(mesh, .name = "ref deck"), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, rb) = CRES_RET(ref_new_checked(mesh, .name = "ref fascia"), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, rr) = CRES_RET(ref_new_checked(mesh, .name = "ref rim"), return EXIT_FAILURE);
        if (failed(ref_plate_emit(rs, 6, 60.0f, n, rf, -p.floor.t, 0.0f, REF_UV_CONCRETE, REF_PLATE_NO_TOP)) ||
            failed(ref_plate_emit(rd, 6, 60.0f, n, rf, 0.0f, REF_DECK_T, REF_UV_FLOOR, REF_PLATE_NO_BOTTOM)) ||
            failed(ref_fascia_level(rb, n, rf, &fwin, 0.9f, REF_UV_CONCRETE)) ||
            failed(ref_rim_level(rr, n, rf, win, nr_win, &len)))
            return EXIT_FAILURE;

        LOCAL_SET(mesh_t, ps) = CRES_RET(ref_placed(rs, f * H), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, pd) = CRES_RET(ref_placed(rd, f * H), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, pb) = CRES_RET(ref_placed(rb, f * H), return EXIT_FAILURE);
        LOCAL_SET(mesh_t, pr) = CRES_RET(ref_placed(rr, f * H + REF_DECK_T), return EXIT_FAILURE);
        if (!parity_check_exact("gallery slab", walk_mesh(&hw, &concrete), ps, frame, true, REF_UV_CONCRETE) ||
            !parity_check_exact("gallery deck", walk_mesh(&hw, &wood), pd, frame, true, REF_UV_FLOOR) ||
            !parity_check("gallery fascia", walk_mesh(&w, &band), pb, frame, false, REF_UV_CONCRETE) ||
            !parity_check("gallery rail", walk_mesh(&w, &slat), pr, frame, false, REF_UV_SLAT))
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int gallery_parity_test(void)
{
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    for (int fi = 0; fi < 2; fi++)
        if (gallery_parity_frame(&frames[fi]))  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Facade
 ****************************************************************************/

/* a hexagonal facade with a portal in each of up to two sides */
struct facade_case {
    pcg_facade_params   p;
    pcg_frame           frame;
    vec2                pts[6];
    size_t              nr_pts;
    size_t              edge[2];
    size_t              nr_edges;
    pcg_opening         open[2];
    float               h;
};

static void facade_case_init(struct facade_case *c)
{
    *c = (struct facade_case){ .nr_pts = 6, .h = 4.0f, .edge = { 1, 4 }, .nr_edges = 2 };
    pcg_facade_params_init(&c->p);
    test_frame(&c->frame, 0.4f, false, 3.0f, 0.5f, -2.0f);
    for (int i = 0; i < 6; i++) ref_ngon_vertex(i, 6, 12.0f, c->pts[i]);

    /* sides of 12: an entrance in the middle of one, a lower door off the middle of another */
    c->open[0] = (pcg_opening){ .at = 6.0f, .w = 2.6f, .h = c->p.wall.head };
    c->open[1] = (pcg_opening){ .at = 3.6f, .w = 2.0f, .h = 2.4f };
}

static void facade_case_edges(const struct facade_case *c, pcg_facade_edge *edges)
{
    for (size_t i = 0; i < c->nr_edges; i++)
        edges[i] = (pcg_facade_edge){ .edge = c->edge[i], .openings = &c->open[i], .nr_openings = 1 };
}

static cerr facade_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct facade_case *c = priv;
    pcg_facade_edge edges[2];

    facade_case_edges(c, edges);

    return pcg_facade_emit(ctx, &c->p, .frame = c->frame, .pts = c->pts, .nr_pts = c->nr_pts, .h = c->h,
                           .edges = c->nr_edges ? edges : nullptr, .nr_edges = c->nr_edges);
}

/* @c's sides, each a window wall from its point to the next, with its portal */
static cerr facade_case_walls(const pcg_ctx *ctx, const void *priv)
{
    const struct facade_case *c = priv;
    pcg_ctx at = pcg_ctx_at(ctx, &c->frame);

    for (size_t i = 0; i < c->nr_pts; i++) {
        pcg_frame run;
        float len = CRES_RET_CERR(pcg_frame_run(&run, c->pts[i], c->pts[(i + 1) % c->nr_pts], 0.0f));
        const pcg_opening *open = nullptr;

        for (size_t e = 0; e < c->nr_edges; e++)
            if (c->edge[e] == i)    open = &c->open[e];

        CERR_RET_CERR(pcg_window_wall_emit(&at, &c->p.wall, .frame = run, .len = len, .h = c->h,
                                           .openings = open, .nr_openings = !!open));
    }

    return CERR_OK;
}

/* @c emits what its sides emit as window walls, the same bytes in the same buckets */
static bool facade_case_ok(const struct facade_case *c)
{
    LOCAL_SET(pcg_sink, fs) = CRES_RET(element_sink(facade_case_emit, c, &glazing_palette, &PCG_FRAME_IDENTITY),
                                       return false);
    LOCAL_SET(pcg_sink, ws) = CRES_RET(element_sink(facade_case_walls, c, &glazing_palette, &PCG_FRAME_IDENTITY),
                                       return false);
    struct sink_walk fw, ww;

    if (!sink_walk_get(fs, &fw) || !sink_walk_get(ws, &ww) || !fw.nr || fw.nr != ww.nr)     return false;
    for (size_t b = 0; b < fw.nr; b++)
        if (fw.key[b].mat != ww.key[b].mat || !meshes_same_bytes(fw.mesh[b], ww.mesh[b]))   return false;

    return element_frames_ok(facade_case_emit, c, &glazing_palette, true);
}

static int facade_test(void)
{
    struct facade_case c;
    facade_case_init(&c);

    /* every side is the generic window wall */
    pcg_window_wall_params wp;
    pcg_window_wall_params_init(&wp);
    if (c.p.wall.t != wp.t || c.p.wall.sill != wp.sill || c.p.wall.head != wp.head || c.p.wall.bay != wp.bay ||
        c.p.wall.open_below != wp.open_below || c.p.wall.glazing.member.skip != wp.glazing.member.skip)
        return EXIT_FAILURE;
    for (int s = 0; s < PCG_WINDOW_WALL_NR_SURF; s++)
        if (c.p.wall.surf[s].role != wp.surf[s].role || c.p.wall.surf[s].collide != wp.surf[s].collide)
            return EXIT_FAILURE;

    /* portals in two sides, in none, and in the last side, with a storefront wall */
    if (!facade_case_ok(&c))                    return EXIT_FAILURE;
    struct facade_case e = c;
    e.nr_edges = 0;
    if (!facade_case_ok(&e))                    return EXIT_FAILURE;
    e = c;
    e.edge[0] = 5;
    e.nr_edges = 1;
    e.p.wall.open_below = true;
    if (!facade_case_ok(&e))                    return EXIT_FAILURE;

    /* the portals take their glass and spandrel out: fewer panes than without them */
    LOCAL_SET(pcg_sink, ds) = CRES_RET(element_sink(facade_case_emit, &c, &glazing_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    e = c;
    e.nr_edges = 0;
    LOCAL_SET(pcg_sink, ns) = CRES_RET(element_sink(facade_case_emit, &e, &glazing_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    struct sink_walk dw, nw;
    if (!sink_walk_get(ds, &dw) || !sink_walk_get(ns, &nw) || dw.nr != 3 || nw.nr != 3)     return EXIT_FAILURE;
    if (mesh_nr_vx(walk_mesh(&nw, &glass)) != 6 * 12)                                       return EXIT_FAILURE;
    /* the entrance splits its side's pane in two, the door leaves a transom over itself */
    if (mesh_nr_vx(walk_mesh(&dw, &glass)) != (6 + 1 + 2) * 12)                             return EXIT_FAILURE;

    /*
     * zeroed or nonsense style, short, missing or broken polygons, bad edge
     * lists, or a side that doesn't fit, even the last one: nothing emitted
     */
    LOCAL_SET(pcg_sink, bad) = CRES_RET(pcg_sink_new("facade"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bad, .palette = &glazing_palette, .frame = PCG_FRAME_IDENTITY };

    struct facade_case z = c;
    z.p = (pcg_facade_params){};
    if (!failed(facade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.nr_pts = 2;
    if (!failed(facade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    memcpy(z.pts[3], z.pts[2], sizeof(vec2));
    if (!failed(facade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.edge[1] = 6;
    if (!failed(facade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.edge[0] = 4;
    z.edge[1] = 1;
    if (!failed(facade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.edge[0] = 4;
    if (!failed(facade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.edge[1] = 5;
    z.open[1] = (pcg_opening){ .at = 6.0f, .w = 11.0f, .h = c.p.wall.head };
    if (!failed(facade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.edge[1] = 5;
    z.open[1].h = c.p.wall.head + 0.5f;
    if (!failed(facade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.h = c.p.wall.head;
    if (!failed(facade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.wall.glazing.member.skip = false;
    z.p.wall.glazing.member_w = 1.2f;
    if (!failed(facade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z = c;
    z.p.wall.glazing.min_seg = -1.0f;
    if (!failed(facade_case_emit(&bc, &z)))     return EXIT_FAILURE;
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = {
            &z.pts[3][0], &z.pts[5][1], &z.h, &z.p.wall.t, &z.p.wall.bay, &z.p.wall.glazing.member_t,
            &z.open[1].at, &z.open[1].h,
        };

        for (size_t k = 0; k < array_size(fields); k++) {
            z = c;
            *fields[k] = nonfinite[i];
            if (!failed_inval(facade_case_emit(&bc, &z)))   return EXIT_FAILURE;
        }
    }
    for (size_t i = 0; i < array_size(bad_frames); i++) {
        z = c;
        z.frame = bad_frames[i];
        if (!failed_inval(facade_case_emit(&bc, &z)))   return EXIT_FAILURE;
    }
    if (!failed(pcg_facade_emit(&bc, &c.p, .frame = c.frame, .nr_pts = 6, .h = c.h)))   return EXIT_FAILURE;
    if (!failed(pcg_facade_emit(&bc, &c.p, .frame = c.frame, .pts = c.pts, .nr_pts = 6, .h = c.h,
                                .nr_edges = 1)))
        return EXIT_FAILURE;
    if (!failed(pcg_facade_emit(nullptr, &c.p, .frame = c.frame, .pts = c.pts, .nr_pts = 6, .h = c.h)))
        return EXIT_FAILURE;
    if (!failed(_pcg_facade_emit(&bc, &c.p, nullptr)) || !failed(pcg_facade_check(nullptr, nullptr)))
        return EXIT_FAILURE;
    if (pcg_sink_get_stats(bad).nr_models)      return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/*
 * The reference building's perimeter as one facade per storey, against
 * reference copies of the window wall and pane emitters called the way
 * the reference building calls them (see window_wall_parity_test()): a
 * hexagon of 0.22 window walls on the 60 m outline moved in by half their
 * thickness, 5.65 tall on the storey's deck, its sides running from one
 * point toward the next; on the ground floor, side 1 gets the 2.6 m
 * entrance in its middle. Each storey is compared on its own, so the
 * ground floor's facade and the upper floors' are both checked. The
 * facade's polygon closes on its first point, so the reference's last
 * side ends there too: the original computes that end a full turn on,
 * which rounds it up to about 1.2e-5 away at this radius, an accepted
 * change (the gallery's rim has the same one).
 */
static int facade_parity_test(void)
{
    const int n = REF_SIDES;
    float apothem = REF_RADIUS * cosf((float)M_PI / n);
    float rin = REF_RADIUS * (apothem - REF_WALL_T * 0.5f) / apothem;
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    pcg_facade_params p;
    pcg_facade_params_init(&p);
    p.wall.t = REF_WALL_T;
    p.wall.sill = REF_SILL_H;
    p.wall.head = REF_HEAD_H;
    p.wall.bay = REF_BAY;
    p.wall.mullion.role = PCG_ROLE_CLADDING;

    vec2 pts[REF_SIDES];
    for (int i = 0; i < n; i++) ref_ngon_vertex(i, n, rin, pts[i]);

    for (int fi = 0; fi < 2; fi++)
        for (int f = 0; f < REF_FLOORS; f++) {
            float y = f * REF_FLOOR_H + REF_DECK_T, entrance_len = 0.0f;
            LOCAL_SET(mesh_t, mw) = CRES_RET(ref_new_checked(mesh, .name = "ref walls"), return EXIT_FAILURE);
            LOCAL_SET(mesh_t, mg) = CRES_RET(ref_new_checked(mesh, .name = "ref glass"), return EXIT_FAILURE);

            for (int i = 0; i < n; i++) {
                const float *v0 = pts[i], *v1 = pts[(i + 1) % n];
                vec2 dir = { v1[0] - v0[0], v1[1] - v0[1] };

                float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1]);

                dir[0] /= len;
                dir[1] /= len;

                int bays = (int)lrintf(len / REF_BAY);
                float gap_w = !f && i == REF_ENTRANCE ? REF_ENTRANCE_W : 0.0f;

                if (gap_w > 0.0f)   entrance_len = len;
                CERR_RET(ref_wall_emit(mw, &(ref_wall_params){
                             .org = { v0[0], v0[1] }, .dir = { dir[0], dir[1] },
                             .len = len, .h = REF_WALL_H, .t = REF_WALL_T,
                             .sill = REF_SILL_H, .head = REF_HEAD_H,
                             .mull_w = 0.09f, .mull_pitch = 1.2f, .post_w = 0.3f,
                             .bays = bays > 1 ? bays : 1,
                             .gap_at = len * 0.5f, .gap_w = gap_w,
                             .uv = REF_UV_PANEL,
                         }),
                         return EXIT_FAILURE);
                CERR_RET(ref_glass_emit(mg, v0, dir, len, REF_SILL_H, REF_HEAD_H, len * 0.5f, gap_w, 0.0f,
                                        REF_UV_BRONZE),
                         return EXIT_FAILURE);
            }

            LOCAL_SET(mesh_t, ref_walls) = CRES_RET(ref_new_checked(mesh, .name = "ref storey walls"),
                                                    return EXIT_FAILURE);
            LOCAL_SET(mesh_t, ref_glass) = CRES_RET(ref_new_checked(mesh, .name = "ref storey glass"),
                                                    return EXIT_FAILURE);
            CERR_RET(ref_instance(ref_walls, mw, (vec3){ 0.0f, y, 0.0f }), return EXIT_FAILURE);
            CERR_RET(ref_instance(ref_glass, mg, (vec3){ 0.0f, y, 0.0f }), return EXIT_FAILURE);

            LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("facade"), return EXIT_FAILURE);
            pcg_ctx ctx = { .sink = sink, .palette = &glazing_palette, .frame = frames[fi] };
            const pcg_opening entrance = { .at = entrance_len * 0.5f, .w = REF_ENTRANCE_W, .h = REF_HEAD_H };
            const pcg_facade_edge edge = { .edge = REF_ENTRANCE, .openings = &entrance, .nr_openings = 1 };
            const pcg_frame storey = { .org = { 0.0f, y, 0.0f }, .dir = { 1.0f, 0.0f } };

            CERR_RET(pcg_facade_emit(&ctx, &p, .frame = storey, .pts = pts, .nr_pts = n, .h = REF_WALL_H,
                                     .edges = f ? nullptr : &edge, .nr_edges = !f),
                     return EXIT_FAILURE);

            struct sink_walk w;
            if (!sink_walk_get(sink, &w) || w.nr != 2)                                  return EXIT_FAILURE;
            if (!parity_check(f ? "facade walls" : "facade walls, ground floor", walk_mesh(&w, &panel), ref_walls,
                              &frames[fi], false, REF_UV_PANEL) ||
                !parity_check(f ? "facade glass" : "facade glass, ground floor", walk_mesh(&w, &glass), ref_glass,
                              &frames[fi], false, REF_UV_BRONZE))
                return EXIT_FAILURE;
        }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Room
 ****************************************************************************/

/* the reference layout's carpet, and a finish for what a visit hook adds */
static const pcg_material carpet_tile = { .name = "carpet", .uv_mode = PCG_UV_METERS };
static const pcg_material shell = { .name = "shell", .uv_mode = PCG_UV_METERS };
static const pcg_palette room_palette = {
    .mat[PCG_ROLE_CLADDING]     = &panel,
    .mat[PCG_ROLE_GLASS]        = &glass,
    .mat[PCG_ROLE_METAL]        = &bronze,
    .mat[PCG_ROLE_CARPET]       = &carpet_tile,
    .mat[PCG_ROLE_FIXTURE]      = &glow,
    .mat[PCG_ROLE_SIGN]         = &decal,
    .mat[PCG_ROLE_FURNITURE]    = &shell,
};

/* what a test visit hook saw, and what it returns */
struct room_visit {
    pcg_frame       frame;
    pcg_room_info   info;
    cerr            ret;
    int             calls;
};

/* record the room, and put a 0.5 m block over its whole interior */
static cerr room_visit_fn(const pcg_ctx *ctx, const pcg_room_info *info, void *priv)
{
    struct room_visit *rv = priv;
    const pcg_surface block = { .role = PCG_ROLE_FURNITURE, .collide = PCG_COLLIDE_NONE };

    rv->calls++;
    rv->frame = ctx->frame;
    rv->info = *info;
    CERR_RET_CERR(rv->ret);

    return pcg_box(ctx, .lo = { 0.0f, 0.0f, 0.0f }, .hi = { info->w, 0.5f, info->d }, .face = PCG_BOX_ALL(&block));
}

/* a 5 by 4 office, 3 m tall, along a yawed line */
struct room_case {
    pcg_room_params p;
    pcg_frame       frame;
    float           a0;
    float           w;
    float           d;
    float           h;
    float           ceiling;
    uint32_t        mask;
    const char      *label;
    bool            party_lo;
    bool            party_hi;
};

#define ROOM_ALL_SOLID \
    (PCG_ROOM_SOLID(PCG_ROOM_FRONT) | PCG_ROOM_SOLID(PCG_ROOM_BACK) | PCG_ROOM_SOLID(PCG_ROOM_LEFT) | \
     PCG_ROOM_SOLID(PCG_ROOM_RIGHT))

static void room_case_init(struct room_case *c)
{
    *c = (struct room_case){
        .a0 = 1.5f, .w = 5.0f, .d = 4.0f, .h = 3.0f, .ceiling = 2.9f, .mask = ROOM_ALL_SOLID, .label = "107",
    };
    pcg_room_params_init(&c->p);
    test_frame(&c->frame, 0.3f, false, 1.0f, 0.2f, -2.0f);
}

static cerr room_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct room_case *c = priv;

    return pcg_room_emit(ctx, &c->p, .frame = c->frame, .a0 = c->a0, .w = c->w, .d = c->d, .h = c->h,
                         .ceiling = c->ceiling, .wall_mask = c->mask, .label = c->label,
                         .tag = { .floor = 1, .segment = 2, .ring = 0, .number = 7 },
                         .party_lo = c->party_lo, .party_hi = c->party_hi);
}

/*
 * @c emits @nr_solid wall boxes, @nr_panes pane pieces in @nr_members
 * member boxes, the carpet and the light, as a closed surface over [@lo,
 * @hi] of its frame, unless they are nullptr, and @nr_labels labels
 */
static bool room_case_ok(const struct room_case *c, size_t nr_solid, size_t nr_panes, size_t nr_members,
                         size_t nr_labels, const float *lo, const float *hi)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(room_case_emit, c, &room_palette, &PCG_FRAME_IDENTITY),
                                         return false);
    struct sink_walk w;

    if (!sink_walk_get(sink, &w) || w.nr != 2 + !!nr_solid + !!nr_panes + !!nr_members)     return false;

    struct mesh *solid = walk_mesh(&w, &panel), *pane = walk_mesh(&w, &glass), *member = walk_mesh(&w, &bronze);
    struct mesh *carpet = walk_mesh(&w, &carpet_tile), *light = walk_mesh(&w, &glow);
    if (nr_solid && (!solid || mesh_nr_vx(solid) != nr_solid * 36))                                     return false;
    if (nr_panes && (!pane || mesh_nr_vx(pane) != nr_panes * 12))                                       return false;
    if (nr_members && (!member || mesh_nr_vx(member) != nr_members * 36))                               return false;
    if (!carpet || mesh_nr_vx(carpet) != 36 || !mesh_y_within(carpet, &c->frame, 0.0f, c->p.carpet_t))  return false;
    if (!light || mesh_nr_vx(light) != (size_t)c->p.light.segments * 12)                                return false;
    if (!mesh_y_within(light, &c->frame, c->ceiling - c->p.light.drop, c->ceiling))                     return false;
    if (!walk_closed(&w))                                                                               return false;

    if (lo && !walk_aabb_is(&w, &c->frame, lo, hi))                                         return false;

    struct label_walk lw;
    if (!label_walk_get(sink, &lw) || lw.nr != nr_labels)                                   return false;
    if (nr_labels && strcmp(lw.label[0].text, c->label))                                    return false;

    return element_frames_ok(room_case_emit, c, &room_palette, true);
}

static int room_test(void)
{
    struct room_case c;
    room_case_init(&c);

    /* the children's defaults, 1.1 by 2.2 doorways, a 12 mm carpet that doesn't collide, no hook */
    pcg_wall_params wp;
    pcg_glazing_params gp;
    pcg_fixture_params fp;
    pcg_sign_params sp;
    pcg_wall_params_init(&wp);
    pcg_glazing_params_init(&gp);
    pcg_fixture_params_init(&fp);
    pcg_sign_params_init(&sp);
    if (c.p.door_w != 1.1f || c.p.door_h != 2.2f || c.p.carpet_t != 0.012f || c.p.visit)    return EXIT_FAILURE;
    if (c.p.carpet.role != PCG_ROLE_CARPET || c.p.carpet.collide != PCG_COLLIDE_NONE)       return EXIT_FAILURE;
    if (c.p.wall.t != wp.t || c.p.wall.face_l.role != wp.face_l.role)                       return EXIT_FAILURE;
    if (c.p.glass.member_w != gp.member_w || c.p.glass.sill != gp.sill)                     return EXIT_FAILURE;
    if (c.p.light.r != fp.r || c.p.light.drop != fp.drop || c.p.sign.h != sp.h)             return EXIT_FAILURE;

    /* four solid walls over the outline: the flanks the whole depth, the front and the back between them */
    float a1 = c.a0 + c.w, ft = c.p.wall.t;
    if (!room_case_ok(&c, 4, 0, 0, 1, (vec3){ c.a0, 0.0f, 0.0f }, (vec3){ a1, c.h, c.d }))  return EXIT_FAILURE;
    LOCAL_SET(pcg_sink, ss) = CRES_RET(element_sink(room_case_emit, &c, &room_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    struct sink_walk w;
    double rt = c.p.wall.t, iw = c.w - 2.0 * rt, id = c.d - 2.0 * rt;
    double vol = 2.0 * rt * c.d * c.h + 2.0 * rt * iw * c.h + iw * id * c.p.carpet_t +
                 cylinder_volume(c.p.light.segments, c.p.light.r, c.p.light.h);
    if (!sink_walk_get(ss, &w) || fabs(walk_volume(&w) - vol) > 1e-4 * vol)                 return EXIT_FAILURE;

    /* doorways front and back: three pieces each */
    struct room_case e = c;
    e.mask |= PCG_ROOM_DOOR(PCG_ROOM_FRONT) | PCG_ROOM_DOOR(PCG_ROOM_BACK);
    if (!room_case_ok(&e, 8, 0, 0, 1, (vec3){ c.a0, 0.0f, 0.0f }, (vec3){ a1, c.h, c.d }))  return EXIT_FAILURE;

    /*
     * a glass front with a doorway, a glass left side, a solid back with a
     * doorway and no right side: three pane pieces with eight members in
     * front, one with four on the left
     */
    e.mask = PCG_ROOM_GLASS(PCG_ROOM_FRONT) | PCG_ROOM_DOOR(PCG_ROOM_FRONT) | PCG_ROOM_GLASS(PCG_ROOM_LEFT) |
             PCG_ROOM_SOLID(PCG_ROOM_BACK) | PCG_ROOM_DOOR(PCG_ROOM_BACK);
    if (!room_case_ok(&e, 3, 4, 12, 1, (vec3){ c.a0 + ft * 0.5f - c.p.glass.member_t * 0.5f, 0.0f, 0.0f },
                      (vec3){ a1, c.h, c.d }))
        return EXIT_FAILURE;

    /* solid wins over glass */
    e.mask = ROOM_ALL_SOLID | PCG_ROOM_GLASS(PCG_ROOM_FRONT) | PCG_ROOM_GLASS(PCG_ROOM_LEFT);
    if (!room_case_ok(&e, 4, 0, 0, 1, nullptr, nullptr))    return EXIT_FAILURE;

    /* party walls are left out, and the rest stays where it was */
    e = c;
    e.party_lo = true;
    if (!room_case_ok(&e, 3, 0, 0, 1, (vec3){ c.a0 + ft, 0.0f, 0.0f }, (vec3){ a1, c.h, c.d })) return EXIT_FAILURE;
    e.party_hi = true;
    if (!room_case_ok(&e, 2, 0, 0, 1, (vec3){ c.a0 + ft, 0.0f, 0.0f }, (vec3){ a1 - ft, c.h, c.d }))
        return EXIT_FAILURE;
    /* a neighbour overlapping @c by a wall's thickness starts where @c's right wall ends, without a slot */
    e = c;
    e.a0 = a1 - ft;
    e.party_lo = true;
    if (!room_case_ok(&e, 3, 0, 0, 1, (vec3){ a1, 0.0f, 0.0f }, (vec3){ e.a0 + e.w, c.h, c.d }))    return EXIT_FAILURE;

    /* no sign without a front wall or a label */
    e = c;
    e.mask &= ~PCG_ROOM_SOLID(PCG_ROOM_FRONT);
    if (!room_case_ok(&e, 3, 0, 0, 0, nullptr, nullptr))    return EXIT_FAILURE;
    e = c;
    e.label = nullptr;
    if (!room_case_ok(&e, 4, 0, 0, 0, nullptr, nullptr))    return EXIT_FAILURE;
    e.label = "";
    if (!room_case_ok(&e, 4, 0, 0, 0, nullptr, nullptr))    return EXIT_FAILURE;

    /* the visit hook: once, in the interior's frame, with the room's measures and tag */
    struct room_visit rv = {};
    e = c;
    e.mask |= PCG_ROOM_DOOR(PCG_ROOM_FRONT);
    e.p.visit = room_visit_fn;
    e.p.visit_priv = &rv;
    LOCAL_SET(pcg_sink, vs) = CRES_RET(element_sink(room_case_emit, &e, &room_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (rv.calls != 1 || !near(rv.info.w, (float)iw, 1e-6f) || !near(rv.info.d, (float)id, 1e-6f))  return EXIT_FAILURE;
    if (rv.info.h != e.ceiling || rv.info.wall_mask != e.mask || rv.info.door_w != e.p.door_w ||
        rv.info.door_h != e.p.door_h)
        return EXIT_FAILURE;
    if (rv.info.tag.floor != 1 || rv.info.tag.segment != 2 || rv.info.tag.ring || rv.info.tag.number != 7)
        return EXIT_FAILURE;
    struct sink_walk bw = { .nr = 1 };
    if (!sink_walk_get(vs, &w) || w.nr != 4 || !(bw.mesh[0] = walk_mesh(&w, &shell)))   return EXIT_FAILURE;
    if (!walk_aabb_is(&bw, &e.frame, (vec3){ e.a0 + ft, 0.0f, ft }, (vec3){ a1 - ft, 0.5f, e.d - ft }))
        return EXIT_FAILURE;
    if (!element_frames_ok(room_case_emit, &e, &room_palette, true))                        return EXIT_FAILURE;

    /* a hook that fails fails the room */
    rv = (struct room_visit){ .ret = CERR_NOT_FOUND };
    LOCAL_SET(pcg_sink, fs) = CRES_RET(pcg_sink_new("room"), return EXIT_FAILURE);
    pcg_ctx fc = { .sink = fs, .palette = &room_palette, .frame = PCG_FRAME_IDENTITY };
    cerr err = room_case_emit(&fc, &e);
    if (!IS_CERR_CODE(err, CERR_NOT_FOUND) || rv.calls != 1)                                return EXIT_FAILURE;

    /*
     * zeroed or nonsense style or site, or a child that doesn't fit, even
     * the last one: nothing emitted, nothing recorded, the hook not called
     */
    rv = (struct room_visit){};
    LOCAL_SET(pcg_sink, bad) = CRES_RET(pcg_sink_new("room"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bad, .palette = &room_palette, .frame = PCG_FRAME_IDENTITY };

    struct room_case z = e;
    z.p = (pcg_room_params){};
    if (!failed(room_case_emit(&bc, &z)))       return EXIT_FAILURE;
    z = e;
    z.w = 2.0f * c.p.wall.t;
    if (!failed(room_case_emit(&bc, &z)))       return EXIT_FAILURE;
    z = e;
    z.d = 0.1f;
    if (!failed(room_case_emit(&bc, &z)))       return EXIT_FAILURE;
    z = e;
    z.p.wall.t = -0.05f;
    if (!failed(room_case_emit(&bc, &z)))       return EXIT_FAILURE;
    z = e;
    z.mask |= 1u << 12;
    if (!failed(room_case_emit(&bc, &z)))       return EXIT_FAILURE;
    /* a doorway too wide for the front, or the back */
    z = e;
    z.p.door_w = 4.85f;
    if (!failed(room_case_emit(&bc, &z)))       return EXIT_FAILURE;
    /* a flank doorway into the front and back walls' ends; a narrower one clears them */
    z = e;
    z.d = 1.2f;
    z.mask = ROOM_ALL_SOLID | PCG_ROOM_DOOR(PCG_ROOM_LEFT);
    if (!failed_inval(room_case_emit(&bc, &z)))     return EXIT_FAILURE;
    z.p.door_w = 1.0f;
    const pcg_room_site zs = {
        .frame = z.frame, .a0 = z.a0, .w = z.w, .d = z.d, .h = z.h, .ceiling = z.ceiling, .wall_mask = z.mask,
    };
    if (failed(pcg_room_check(&z.p, &zs)))      return EXIT_FAILURE;
    z = e;
    z.mask = ROOM_ALL_SOLID | PCG_ROOM_GLASS(PCG_ROOM_RIGHT);
    z.mask &= ~PCG_ROOM_SOLID(PCG_ROOM_RIGHT);
    z.p.glass.member_w = 2.0f;
    if (!failed(room_case_emit(&bc, &z)))       return EXIT_FAILURE;
    z = e;
    z.ceiling = c.p.light.drop * 0.5f;
    if (!failed(room_case_emit(&bc, &z)))       return EXIT_FAILURE;
    z = e;
    z.p.sign.door_offset = 3.0f;
    if (!failed(room_case_emit(&bc, &z)))       return EXIT_FAILURE;
    z = e;
    z.p.light.segments = 2;
    if (!failed(room_case_emit(&bc, &z)))       return EXIT_FAILURE;
    z = e;
    z.p.light.segments = 357913942;     /* 12 vertices a side wrap a 32-bit size_t */
    if (!failed(room_case_emit(&bc, &z)))       return EXIT_FAILURE;
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = {
            &z.p.door_w, &z.p.door_h, &z.p.carpet_t, &z.p.wall.t, &z.p.glass.member_t, &z.p.light.r,
            &z.p.sign.center_y, &z.a0, &z.w, &z.d, &z.h, &z.ceiling,
        };

        for (size_t k = 0; k < array_size(fields); k++) {
            z = e;
            z.mask |= PCG_ROOM_GLASS(PCG_ROOM_RIGHT);
            z.mask &= ~PCG_ROOM_SOLID(PCG_ROOM_RIGHT);
            *fields[k] = nonfinite[i];
            if (!failed_inval(room_case_emit(&bc, &z)))     return EXIT_FAILURE;
        }
    }
    /* with no wall to check, the light and the carpet still go through the frame */
    for (size_t i = 0; i < array_size(bad_frames); i++) {
        z = e;
        z.frame = bad_frames[i];
        if (!failed_inval(room_case_emit(&bc, &z)))         return EXIT_FAILURE;
        z.mask = 0;
        if (!failed_inval(room_case_emit(&bc, &z)))         return EXIT_FAILURE;
    }
    if (!failed(pcg_room_emit(nullptr, &c.p, .w = c.w, .d = c.d, .h = c.h, .ceiling = c.ceiling)))  return EXIT_FAILURE;
    if (!failed(_pcg_room_emit(&bc, &c.p, nullptr)) || !failed(pcg_room_check(nullptr, nullptr)))   return EXIT_FAILURE;

    pcg_sink_stats st = pcg_sink_get_stats(bad);
    if (st.nr_models || st.nr_labels || rv.calls)   return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* a room's interior as the original published it to its hook: its corner in (x, y, z), run and size */
struct room_expect {
    vec3    corner;
    vec2    dir;
    float   w;
    float   d;
};

/* what the rooms emit into, the reference meshes and plaques they are compared against, and the hook's tally */
struct room_parity {
    pcg_ctx             ctx;
    pcg_room_row_params row;
    struct mesh         *ref_solid;
    struct mesh         *ref_glass;
    struct mesh         *ref_frames;
    struct mesh         *ref_lights;
    struct mesh         *ref_carpet;
    struct ref_plaque   plaque[LABELS_MAX];
    size_t              nr_plaques;
    /* the interiors of the rooms about to be emitted, in order, and the next one's index */
    struct room_expect  expect[REF_RING_ROOMS_MAX];
    size_t              next;
    size_t              nr_visits;
    float               corner_err;
    int                 variant;
    bool                bad_visit;
};

/* the hook gets the room's interior: its corner and run where the original's room info put them */
static cerr room_parity_visit(const pcg_ctx *ctx, const pcg_room_info *info, void *priv)
{
    struct room_parity *rp = priv;
    vec3 corner, dir;

    rp->nr_visits++;
    if (rp->next >= REF_RING_ROOMS_MAX) {
        rp->bad_visit = true;
        return CERR_OK;
    }

    const struct room_expect *e = &rp->expect[rp->next++];
    pcg_frame_point(&rp->ctx.frame, e->corner, corner);
    pcg_frame_vector(&rp->ctx.frame, (vec3){ e->dir[0], 0.0f, e->dir[1] }, dir);
    rp->corner_err = fmaxf(rp->corner_err, max_diff(corner, ctx->frame.org, 3));
    if (!near(dir[0], ctx->frame.dir[0], 1e-5f) || !near(dir[2], ctx->frame.dir[1], 1e-5f) || ctx->frame.mirror ||
        !near(info->w, e->w, 1e-5f) || !near(info->d, e->d, 1e-5f))
        rp->bad_visit = true;

    return CERR_OK;
}

/*
 * One ring segment's reference meshes at its storey's deck @y into @rp,
 * its plaques, and its rooms' interiors as the original published them
 */
static cerr room_parity_ref(struct room_parity *rp, const ref_room_rec *rooms, size_t nr_rooms,
                            const ref_room_meshes *m, float y)
{
    CERR_RET_CERR(ref_instance(rp->ref_solid, m->solid, (vec3){ 0.0f, y, 0.0f }));
    CERR_RET_CERR(ref_instance(rp->ref_glass, m->glass, (vec3){ 0.0f, y, 0.0f }));
    CERR_RET_CERR(ref_instance(rp->ref_frames, m->frames, (vec3){ 0.0f, y, 0.0f }));
    CERR_RET_CERR(ref_instance(rp->ref_lights, m->lights, (vec3){ 0.0f, y, 0.0f }));
    CERR_RET_CERR(ref_instance(rp->ref_carpet, m->carpet, (vec3){ 0.0f, y, 0.0f }));
    if (nr_rooms > REF_RING_ROOMS_MAX)  return CERR_TOO_LARGE;

    for (size_t k = 0; k < nr_rooms; k++) {
        const ref_room_rec *rr = &rooms[k];
        float rt = REF_ROOM_T;
        vec2 in = { -rr->dir[1], rr->dir[0] };

        if (rr->has_plaque) {
            if (rp->nr_plaques == LABELS_MAX)   return CERR_TOO_LARGE;

            struct ref_plaque *pl = &rp->plaque[rp->nr_plaques++];
            *pl = (struct ref_plaque){
                .org = { rr->plaque[0], rr->plaque[1] }, .dir = { rr->dir[0], rr->dir[1] }, .y = y,
            };
            memcpy(pl->text, rr->text, sizeof(pl->text));
        }

        /* the original's room info: the interior corner in (x, z), on the deck */
        rp->expect[k] = (struct room_expect){
            .corner = { rr->v0[0] + rr->dir[0] * (rr->a0 + rt) + in[0] * rt, y,
                        rr->v0[1] + rr->dir[1] * (rr->a0 + rt) + in[1] * rt },
            .dir    = { rr->dir[0], rr->dir[1] },
            .w      = rr->a1 - rr->a0 - 2.0f * rt,
            .d      = rr->cf - 2.0f * rt,
        };
    }
    rp->next = 0;

    return CERR_OK;
}

/* everything @sink took matches what @rp collected: 115 rooms, their meshes, plaques and interiors */
static bool room_parity_ok(const struct room_parity *rp, pcg_sink *sink, const pcg_frame *f, const char *what)
{
    if (rp->nr_visits != 115 || rp->bad_visit || rp->corner_err > PARITY_EPS)   return false;

    struct sink_walk w;
    char name[64];
    if (!sink_walk_get(sink, &w) || w.nr != (rp->variant ? 3 : 5))  return false;

#define ROOM_PARITY(_part, _mat, _ref, _density) \
    (snprintf(name, sizeof(name), "%s %s", what, _part), \
     parity_check(name, walk_mesh(&w, (_mat)), (_ref), f, false, (_density)))
    if (!ROOM_PARITY("walls", &panel, rp->ref_solid, REF_UV_PANEL) ||
        !ROOM_PARITY("carpet", &carpet_tile, rp->ref_carpet, REF_UV_CARPET) ||
        !ROOM_PARITY("lights", &glow, rp->ref_lights, 1.0f))
        return false;
    if (!rp->variant &&
        (!ROOM_PARITY("glass", &glass, rp->ref_glass, REF_UV_BRONZE) ||
         !ROOM_PARITY("frames", &bronze, rp->ref_frames, REF_UV_BRONZE)))
        return false;
#undef ROOM_PARITY

    LOCAL_SET(void, wbuf) = mem_alloc(sizeof(struct label_walk));
    struct label_walk *lw = wbuf;
    if (!lw || !label_walk_get(sink, lw) || lw->nr != rp->nr_plaques)   return false;
    if (!rp->variant && lw->nr != 97)                                   return false;

    float err = 0.0f, nerr = 0.0f;
    for (size_t k = 0; k < lw->nr; k++)
        if (!plaque_near(&lw->label[k], &rp->plaque[k], f, &err, &nerr))    return false;
    if (verbose)
        printf("%s plaques: %zu, max error p %g n %g; interior corners: max error %g\n", what, lw->nr, err, nerr,
               rp->corner_err);

    return err <= PARITY_EPS && nerr <= PARITY_DIR_EPS;
}

/* ready @rp for @variant at @f, into @sink, the reference meshes into @ref: solid, glass, frames, lights, carpet */
static void room_parity_init(struct room_parity *rp, pcg_sink *sink, const pcg_frame *f, int variant,
                             struct mesh *const ref[5])
{
    *rp = (struct room_parity){
        .ctx        = { .sink = sink, .palette = &room_palette, .frame = *f },
        .ref_solid  = ref[0],
        .ref_glass  = ref[1],
        .ref_frames = ref[2],
        .ref_lights = ref[3],
        .ref_carpet = ref[4],
        .variant    = variant,
    };
}

static cerr room_parity_ring(const ref_room_wall *rw, size_t nr, const ref_room_rec *rooms, size_t nr_rooms,
                             const ref_room_meshes *m, float y, void *priv)
{
    struct room_parity *rp = priv;

    CERR_RET_CERR(room_parity_ref(rp, rooms, nr_rooms, m, y));

    for (size_t k = 0; k < nr_rooms; k++) {
        const ref_room_rec *rr = &rooms[k];
        const pcg_frame line = { .org = { rr->v0[0], y, rr->v0[1] }, .dir = { rr->dir[0], rr->dir[1] } };

        CERR_RET_CERR(pcg_room_emit(&rp->ctx, &rp->row.room, .frame = line, .a0 = rr->a0, .w = rr->a1 - rr->a0,
                                    .d = rr->cf, .h = REF_WALL_H, .ceiling = REF_SOFFIT_H,
                                    .wall_mask = rr->wall_mask, .label = rr->text));
    }

    return CERR_OK;
}

/*
 * The reference building's rooms, one by one, against the reference
 * copy of the room strip that emits them inline: the two-room outer ring
 * and the three-room inner one with its carousel, on every storey's deck
 * but the ground floor's entrance segment, and once more with every wall
 * solid and doored front and back. Each room takes its line, start,
 * width, depth and wall mask from the strip; its walls are 0.09 thick and
 * 5.65 tall, its light hangs from the 5.6 soffit, and its carpet is
 * 12 mm thick, all faces at a quarter repeat per meter. Its plaque is
 * compared as sign_parity_test() compares them, and its visit hook must
 * get the interior corner the original published, once per room: 115
 * rooms.
 */
static int room_parity_test(void)
{
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    for (int fi = 0; fi < 2; fi++)
        for (int variant = 0; variant < 2; variant++) {
            LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("room"), return EXIT_FAILURE);
            LOCAL_SET(mesh_t, solid) = CRES_RET(ref_new_checked(mesh, .name = "ref solid"), return EXIT_FAILURE);
            LOCAL_SET(mesh_t, panes) = CRES_RET(ref_new_checked(mesh, .name = "ref glass"), return EXIT_FAILURE);
            LOCAL_SET(mesh_t, frame) = CRES_RET(ref_new_checked(mesh, .name = "ref frames"), return EXIT_FAILURE);
            LOCAL_SET(mesh_t, lights) = CRES_RET(ref_new_checked(mesh, .name = "ref lights"), return EXIT_FAILURE);
            LOCAL_SET(mesh_t, carpet) = CRES_RET(ref_new_checked(mesh, .name = "ref carpet"), return EXIT_FAILURE);
            LOCAL_SET(void, buf) = mem_alloc(sizeof(struct room_parity));
            struct room_parity *rp = buf;
            if (!rp)    return EXIT_FAILURE;

            room_parity_init(rp, sink, &frames[fi], variant,
                             (struct mesh *const[5]){ solid, panes, frame, lights, carpet });
            pcg_room_row_params_init(&rp->row);
            rp->row.room.visit = room_parity_visit;
            rp->row.room.visit_priv = rp;

            CERR_RET(ref_rooms_for_each(variant, room_parity_ring, rp), return EXIT_FAILURE);
            if (!room_parity_ok(rp, sink, &frames[fi], "room"))     return EXIT_FAILURE;
        }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Room row
 ****************************************************************************/

/* what a test room fn saw */
struct row_fn_rec {
    pcg_room_tag    tag[16];
    size_t          nr;
    int             bad_segment;
};

/*
 * The middle room's fn: record its tag, give it a solid back and no
 * carpet, and on @bad_segment a width that can't be
 */
static void row_case_fn(const pcg_room_tag *tag, void *priv, pcg_room_entry *room, pcg_room_params *p)
{
    struct row_fn_rec *rec = priv;

    if (rec->nr < array_size(rec->tag))     rec->tag[rec->nr++] = *tag;
    room->wall_mask |= PCG_ROOM_SOLID(PCG_ROOM_BACK);
    p->carpet.skip = true;
    if (tag->segment == rec->bad_segment)   room->width = -1.0f;
}

/* every place on a sign */
static void row_case_label(const pcg_room_tag *tag, void *priv, char *buf, size_t size)
{
    snprintf(buf, size, "%d-%d-%d-%d", tag->floor, tag->segment, tag->ring, tag->number);
}

/* three rooms on a 20 m line: 3 m, a quarter and 4 m wide, centered */
struct row_case {
    pcg_room_row_params p;
    pcg_frame           frame;
    float               len;
    float               depth_ref;
    pcg_room_entry      rooms[3];
    size_t              nr_rooms;
};

static cerr row_case_visit(const pcg_ctx *ctx, const pcg_room_info *info, void *priv)
{
    int *visits = priv;

    (*visits)++;

    return CERR_OK;
}

static void row_case_init(struct row_case *c, struct row_fn_rec *rec, int *visits)
{
    *c = (struct row_case){ .len = 20.0f, .depth_ref = 10.0f, .nr_rooms = 3 };
    *rec = (struct row_fn_rec){ .bad_segment = -1 };
    pcg_room_row_params_init(&c->p);
    c->p.label = row_case_label;
    c->p.room.visit = row_case_visit;
    c->p.room.visit_priv = visits;
    test_frame(&c->frame, -0.7f, false, 2.0f, 0.3f, 4.0f);

    c->rooms[0] = (pcg_room_entry){
        .width = 3.0f, .depth = 4.0f, .wall_mask = ROOM_ALL_SOLID | PCG_ROOM_DOOR(PCG_ROOM_FRONT),
    };
    c->rooms[1] = (pcg_room_entry){
        .width = 0.25f, .width_frac = true, .depth = 0.5f, .depth_frac = true,
        .wall_mask = PCG_ROOM_SOLID(PCG_ROOM_LEFT) | PCG_ROOM_SOLID(PCG_ROOM_RIGHT) | PCG_ROOM_GLASS(PCG_ROOM_FRONT),
        .fn = row_case_fn, .priv = rec,
    };
    c->rooms[2] = (pcg_room_entry){ .width = 4.0f, .depth = 3.0f, .wall_mask = ROOM_ALL_SOLID };
}

static const pcg_room_tag row_case_tag = { .floor = 2, .segment = 3, .ring = 1, .number = 7 };

static cerr row_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct row_case *c = priv;

    return pcg_room_row_emit(ctx, &c->p, .frame = c->frame, .len = c->len, .depth_ref = c->depth_ref, .h = 3.0f,
                             .ceiling = 2.9f, .rooms = c->rooms, .nr_rooms = c->nr_rooms, .tag = row_case_tag);
}

/* @c's rooms emitted one by one, where the row lays them out, the middle one as its fn picks it */
static cerr row_case_rooms(const pcg_ctx *ctx, const void *priv)
{
    const struct row_case *c = priv;
    const float a0[3] = { 4.0f, 7.0f, 12.0f }, w[3] = { 3.0f, 5.0f, 4.0f }, d[3] = { 4.0f, 5.0f, 3.0f };

    for (size_t k = 0; k < 3; k++) {
        pcg_room_params p = c->p.room;
        pcg_room_tag tag = row_case_tag;
        char label[PCG_ROOM_LABEL_MAX];
        uint32_t mask = c->rooms[k].wall_mask;

        tag.number += (int)k;
        if (k == 1) {
            mask |= PCG_ROOM_SOLID(PCG_ROOM_BACK);
            p.carpet.skip = true;
        }
        row_case_label(&tag, nullptr, label, sizeof(label));
        CERR_RET_CERR(pcg_room_emit(ctx, &p, .frame = c->frame, .a0 = a0[k], .w = w[k], .d = d[k], .h = 3.0f,
                                    .ceiling = 2.9f, .wall_mask = mask, .label = label, .tag = tag));
    }

    return CERR_OK;
}

/* @a and @b took the same bytes into the same buckets, and the same labels */
static bool sinks_same(pcg_sink *a, pcg_sink *b)
{
    struct sink_walk wa, wb;

    if (!sink_walk_get(a, &wa) || !sink_walk_get(b, &wb) || !wa.nr || wa.nr != wb.nr)       return false;
    for (size_t i = 0; i < wa.nr; i++)
        if (wa.key[i].mat != wb.key[i].mat || !meshes_same_bytes(wa.mesh[i], wb.mesh[i]))   return false;

    LOCAL_SET(void, la) = mem_alloc(sizeof(struct label_walk));
    LOCAL_SET(void, lb) = mem_alloc(sizeof(struct label_walk));
    struct label_walk *ka = la, *kb = lb;
    if (!ka || !kb || !label_walk_get(a, ka) || !label_walk_get(b, kb) || ka->nr != kb->nr) return false;
    for (size_t i = 0; i < ka->nr; i++)
        if (strcmp(ka->label[i].text, kb->label[i].text) || ka->label[i].h != kb->label[i].h ||
            memcmp(&ka->label[i].frame, &kb->label[i].frame, sizeof(pcg_frame)))
            return false;

    return true;
}

/*
 * Fractions that fill the line fit it, whatever the line's length does
 * to their rounding: fifths, thirds and tenths over lengths of 20 to 70
 */
static int room_row_fill_test(void)
{
    const struct {
        float   frac;
        size_t  nr;
    } fill[] = { { 0.2f, 5 }, { 1.0f / 3.0f, 3 }, { 0.1f, 10 } };
    pcg_room_row_params p;
    pcg_room_entry rooms[10];

    pcg_room_row_params_init(&p);
    for (size_t f = 0; f < array_size(fill); f++) {
        for (size_t k = 0; k < fill[f].nr; k++)
            rooms[k] = (pcg_room_entry){
                .width = fill[f].frac, .width_frac = true, .depth = 4.0f, .wall_mask = ROOM_ALL_SOLID,
            };

        for (int i = 0; i < 40; i++) {
            float len = 20.0f + (float)i * 1.2589f;
            LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("row"), return EXIT_FAILURE);
            pcg_ctx ctx = { .sink = sink, .palette = &room_palette, .frame = PCG_FRAME_IDENTITY };

            if (failed(pcg_room_row_emit(&ctx, &p, .frame = PCG_FRAME_IDENTITY, .len = len, .h = 3.0f,
                                         .ceiling = 2.9f, .rooms = rooms, .nr_rooms = fill[f].nr)))
                return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

static int room_row_test(void)
{
    struct row_fn_rec rec;
    int visits = 0;
    struct row_case c;
    row_case_init(&c, &rec, &visits);

    pcg_room_row_params dp;
    pcg_room_row_params_init(&dp);
    if (dp.label || dp.room.door_w != 1.1f || dp.room.carpet.role != PCG_ROLE_CARPET)   return EXIT_FAILURE;

    /*
     * the rooms side by side from 4 m on, the middle one picked by its fn
     * and numbered 8, each the room it would be alone, signs included
     */
    LOCAL_SET(pcg_sink, rs) = CRES_RET(element_sink(row_case_emit, &c, &room_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    const pcg_room_tag middle = { .floor = 2, .segment = 3, .ring = 1, .number = 8 };
    if (rec.nr != 1 || memcmp(&rec.tag[0], &middle, sizeof(middle)) || visits != 3) return EXIT_FAILURE;
    LOCAL_SET(pcg_sink, ks) = CRES_RET(element_sink(row_case_rooms, &c, &room_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sinks_same(rs, ks) || pcg_sink_get_stats(rs).nr_labels != 3)                       return EXIT_FAILURE;
    if (!element_frames_ok(row_case_emit, &c, &room_palette, true))                         return EXIT_FAILURE;

    /* no rooms, nothing emitted */
    struct row_case e = c;
    e.nr_rooms = 0;
    LOCAL_SET(pcg_sink, es) = CRES_RET(pcg_sink_new("row"), return EXIT_FAILURE);
    pcg_ctx ec = { .sink = es, .palette = &room_palette, .frame = PCG_FRAME_IDENTITY };
    if (failed(row_case_emit(&ec, &e)) || pcg_sink_get_stats(es).nr_models)                 return EXIT_FAILURE;

    /*
     * zeroed style, a bad line, rooms missing, too wide in all, of a bad
     * width or depth as given or picked, or a last room that doesn't fit:
     * nothing emitted, nothing recorded, no room visited
     */
    LOCAL_SET(pcg_sink, bad) = CRES_RET(pcg_sink_new("row"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bad, .palette = &room_palette, .frame = PCG_FRAME_IDENTITY };
    visits = 0;

    struct row_case z = c;
    z.p = (pcg_room_row_params){};
    if (!failed(row_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.len = 8.0f;
    if (!failed(row_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.rooms[2].width = 0.0f;
    if (!failed(row_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.rooms[2].width = 0.15f;
    if (!failed(row_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    z.rooms[2].depth = 0.1f;
    if (!failed(row_case_emit(&bc, &z)))    return EXIT_FAILURE;
    z = c;
    rec.bad_segment = row_case_tag.segment;
    if (!failed(row_case_emit(&bc, &z)))    return EXIT_FAILURE;
    rec.bad_segment = -1;
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = { &z.len, &z.depth_ref, &z.rooms[0].depth, &z.rooms[2].width, &z.p.room.door_w };

        for (size_t k = 0; k < array_size(fields); k++) {
            z = c;
            *fields[k] = nonfinite[i];
            if (!failed_inval(row_case_emit(&bc, &z)))  return EXIT_FAILURE;
        }
    }
    for (size_t i = 0; i < array_size(bad_frames); i++) {
        z = c;
        z.frame = bad_frames[i];
        if (!failed_inval(row_case_emit(&bc, &z)))  return EXIT_FAILURE;
    }
    if (!failed(pcg_room_row_emit(&bc, &c.p, .frame = c.frame, .len = c.len, .depth_ref = c.depth_ref, .h = 3.0f,
                                  .ceiling = 2.9f, .nr_rooms = 3)))
        return EXIT_FAILURE;
    if (!failed(pcg_room_row_emit(nullptr, &c.p, .len = c.len)) || !failed(_pcg_room_row_emit(&bc, &c.p, nullptr)))
        return EXIT_FAILURE;

    pcg_sink_stats st = pcg_sink_get_stats(bad);
    if (st.nr_models || st.nr_labels || visits)     return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* a hexagonal ring of 15 m sides around a yawed frame, sides 1 and 4 left out */
struct ring_case {
    struct row_case row;
    vec2            pts[6];
    size_t          nr_pts;
    size_t          skip[2];
    size_t          nr_skip;
};

static void ring_case_init(struct ring_case *c, struct row_fn_rec *rec, int *visits)
{
    row_case_init(&c->row, rec, visits);
    c->nr_pts = 6;
    for (int i = 0; i < 6; i++) ref_ngon_vertex(i, 6, 15.0f, c->pts[i]);
    c->skip[0] = 1;
    c->skip[1] = 4;
    c->nr_skip = 2;
}

static const pcg_room_tag ring_case_tag = { .floor = 1, .ring = 2, .number = 1 };

static cerr ring_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct ring_case *c = priv;

    return pcg_room_ring_emit(ctx, &c->row.p, .frame = c->row.frame, .pts = c->pts, .nr_pts = c->nr_pts,
                              .depth_ref = c->row.depth_ref, .h = 3.0f, .ceiling = 2.9f, .rooms = c->row.rooms,
                              .nr_rooms = c->row.nr_rooms, .skip = c->nr_skip ? c->skip : nullptr,
                              .nr_skip = c->nr_skip, .tag = ring_case_tag);
}

/* @c's rows emitted one by one, along the sides it doesn't skip, numbered on */
static cerr ring_case_rows(const pcg_ctx *ctx, const void *priv)
{
    const struct ring_case *c = priv;
    pcg_room_tag tag = ring_case_tag;

    for (size_t i = 0; i < c->nr_pts; i++) {
        pcg_frame run, line;
        float len = CRES_RET_CERR(pcg_frame_run(&run, c->pts[i], c->pts[(i + 1) % c->nr_pts], 0.0f));

        if (i == c->skip[0] || i == c->skip[1])     continue;

        pcg_frame_compose(&c->row.frame, &run, &line);
        tag.segment = (int)i;
        CERR_RET_CERR(pcg_room_row_emit(ctx, &c->row.p, .frame = line, .len = len, .depth_ref = c->row.depth_ref,
                                        .h = 3.0f, .ceiling = 2.9f, .rooms = c->row.rooms,
                                        .nr_rooms = c->row.nr_rooms, .tag = tag));
        tag.number += (int)c->row.nr_rooms;
    }

    return CERR_OK;
}

static int room_ring_test(void)
{
    struct row_fn_rec rec;
    int visits = 0;
    struct ring_case c;
    ring_case_init(&c, &rec, &visits);
    c.row.rooms[2].width = 0.15f;
    c.row.rooms[2].width_frac = true;

    /* a row on every side but the skipped ones, numbered on from row to row */
    LOCAL_SET(pcg_sink, rs) = CRES_RET(element_sink(ring_case_emit, &c, &room_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (rec.nr != 4 || visits != 12)    return EXIT_FAILURE;
    const int segments[4] = { 0, 2, 3, 5 };
    for (int k = 0; k < 4; k++)
        if (rec.tag[k].segment != segments[k] || rec.tag[k].number != 2 + 3 * k || rec.tag[k].floor != 1 ||
            rec.tag[k].ring != 2)
            return EXIT_FAILURE;
    LOCAL_SET(pcg_sink, ks) = CRES_RET(element_sink(ring_case_rows, &c, &room_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sinks_same(rs, ks) || pcg_sink_get_stats(rs).nr_labels != 12)                      return EXIT_FAILURE;
    if (!element_frames_ok(ring_case_emit, &c, &room_palette, true))                        return EXIT_FAILURE;

    /*
     * a short, missing or broken polygon, a bad skip list, or a room that
     * doesn't fit on the last side: nothing emitted, nothing recorded, no
     * room visited
     */
    LOCAL_SET(pcg_sink, bad) = CRES_RET(pcg_sink_new("ring"), return EXIT_FAILURE);
    pcg_ctx bc = { .sink = bad, .palette = &room_palette, .frame = PCG_FRAME_IDENTITY };
    visits = 0;

    struct ring_case z = c;
    z.nr_pts = 2;
    if (!failed(ring_case_emit(&bc, &z)))   return EXIT_FAILURE;
    z = c;
    memcpy(z.pts[3], z.pts[2], sizeof(vec2));
    if (!failed(ring_case_emit(&bc, &z)))   return EXIT_FAILURE;
    z = c;
    z.skip[1] = 6;
    if (!failed(ring_case_emit(&bc, &z)))   return EXIT_FAILURE;
    z = c;
    rec.bad_segment = 5;
    if (!failed(ring_case_emit(&bc, &z)))   return EXIT_FAILURE;
    rec.bad_segment = -1;
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < array_size(nonfinite); i++) {
        float *fields[] = { &z.pts[4][0], &z.pts[5][1], &z.row.depth_ref, &z.row.rooms[1].width };

        for (size_t k = 0; k < array_size(fields); k++) {
            z = c;
            *fields[k] = nonfinite[i];
            if (!failed_inval(ring_case_emit(&bc, &z)))     return EXIT_FAILURE;
        }
    }
    for (size_t i = 0; i < array_size(bad_frames); i++) {
        z = c;
        z.row.frame = bad_frames[i];
        if (!failed_inval(ring_case_emit(&bc, &z)))         return EXIT_FAILURE;
    }
    if (!failed(pcg_room_ring_emit(&bc, &c.row.p, .nr_pts = 6, .rooms = c.row.rooms, .nr_rooms = 3)))
        return EXIT_FAILURE;
    if (!failed(pcg_room_ring_emit(&bc, &c.row.p, .pts = c.pts, .nr_pts = 6, .rooms = c.row.rooms, .nr_rooms = 3,
                                   .h = 3.0f, .ceiling = 2.9f, .nr_skip = 1)))
        return EXIT_FAILURE;
    if (!failed(_pcg_room_ring_emit(&bc, &c.row.p, nullptr)))   return EXIT_FAILURE;

    pcg_sink_stats st = pcg_sink_get_stats(bad);
    if (st.nr_models || st.nr_labels || visits)     return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* the reference building's room rings as row entries: offices on the outside, glazed both ways inside */
#define ROW_OUTER_MASK \
    (PCG_ROOM_SOLID(PCG_ROOM_LEFT) | PCG_ROOM_SOLID(PCG_ROOM_RIGHT) | PCG_ROOM_GLASS(PCG_ROOM_FRONT) | \
     PCG_ROOM_DOOR(PCG_ROOM_FRONT))
#define ROW_INNER_MASK \
    (PCG_ROOM_SOLID(PCG_ROOM_LEFT) | PCG_ROOM_SOLID(PCG_ROOM_RIGHT) | PCG_ROOM_GLASS(PCG_ROOM_FRONT) | \
     PCG_ROOM_GLASS(PCG_ROOM_BACK))

/* the inner rooms' carousel: a different side dropped per segment and floor */
static void row_carousel(const pcg_room_tag *tag, void *priv, pcg_room_entry *room, pcg_room_params *p)
{
    uint32_t side = (uint32_t)(tag->segment + tag->floor) & 3u;

    room->wall_mask &= ~(PCG_ROOM_SOLID(side) | PCG_ROOM_GLASS(side));
}

/* every wall solid, doored front and back */
static void row_all_solid(const pcg_room_tag *tag, void *priv, pcg_room_entry *room, pcg_room_params *p)
{
    room->wall_mask = ROOM_ALL_SOLID | PCG_ROOM_DOOR(PCG_ROOM_FRONT) | PCG_ROOM_DOOR(PCG_ROOM_BACK);
}

/* the reference building's plaque text: the floor and a two-digit room number */
static void row_room_number(const pcg_room_tag *tag, void *priv, char *buf, size_t size)
{
    snprintf(buf, size, "%d%02d", tag->floor, tag->number);
}

static cerr row_parity_ring(const ref_room_wall *rw, size_t nr, const ref_room_rec *rooms, size_t nr_rooms,
                            const ref_room_meshes *m, float y, void *priv)
{
    struct room_parity *rp = priv;

    CERR_RET_CERR(room_parity_ref(rp, rooms, nr_rooms, m, y));
    if (!nr_rooms)  return CERR_OK;

    const ref_room_rec *r0 = &rooms[0];
    pcg_room_entry entries[3];
    size_t ne = r0->ring ? 3 : 2;

    for (size_t k = 0; k < ne; k++)
        entries[k] = (pcg_room_entry){
            .width      = r0->ring ? 0.2f : 0.3f,
            .width_frac = true,
            .depth      = r0->ring ? 3.6f : 4.2f,
            .wall_mask  = r0->ring ? ROW_INNER_MASK : ROW_OUTER_MASK,
            .fn         = rp->variant ? row_all_solid : r0->ring ? row_carousel : nullptr,
        };

    const pcg_frame line = { .org = { r0->v0[0], y, r0->v0[1] }, .dir = { r0->dir[0], r0->dir[1] } };
    return pcg_room_row_emit(&rp->ctx, &rp->row, .frame = line, .len = r0->side_len, .depth_ref = REF_RADIUS,
                             .h = REF_WALL_H, .ceiling = REF_SOFFIT_H, .rooms = entries, .nr_rooms = ne,
                             .tag = { .floor = r0->floor, .segment = r0->segment, .ring = r0->ring,
                                      .number = r0->number });
}

/*
 * The reference building's room rings as rows, against the reference
 * copy of the room strip, as room_parity_test() compares single rooms:
 * the outer ring's two offices 0.3 of their segment wide and 4.2 deep,
 * glass-fronted with a doorway, and the inner ring's three 0.2 wide and
 * 3.6 deep, glazed front and back, with the carousel dropping a side per
 * floor and segment, on every storey, the ground floor included, but its
 * entrance segment; and once more with a fn turning every wall solid and
 * doored front and back. Each row takes its segment's line and length
 * from the reference layout, and numbers its rooms on from the storey's
 * previous rows, its label fn writing the reference building's numbers.
 */
static int room_row_parity_test(void)
{
    pcg_frame frames[2] = { PCG_FRAME_IDENTITY };
    test_frame(&frames[1], 0.6f, false, 5.0f, 12.0f, -3.0f);

    for (int fi = 0; fi < 2; fi++)
        for (int variant = 0; variant < 2; variant++) {
            LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("room row"), return EXIT_FAILURE);
            LOCAL_SET(mesh_t, solid) = CRES_RET(ref_new_checked(mesh, .name = "ref solid"), return EXIT_FAILURE);
            LOCAL_SET(mesh_t, panes) = CRES_RET(ref_new_checked(mesh, .name = "ref glass"), return EXIT_FAILURE);
            LOCAL_SET(mesh_t, frame) = CRES_RET(ref_new_checked(mesh, .name = "ref frames"), return EXIT_FAILURE);
            LOCAL_SET(mesh_t, lights) = CRES_RET(ref_new_checked(mesh, .name = "ref lights"), return EXIT_FAILURE);
            LOCAL_SET(mesh_t, carpet) = CRES_RET(ref_new_checked(mesh, .name = "ref carpet"), return EXIT_FAILURE);
            LOCAL_SET(void, buf) = mem_alloc(sizeof(struct room_parity));
            struct room_parity *rp = buf;
            if (!rp)    return EXIT_FAILURE;

            room_parity_init(rp, sink, &frames[fi], variant,
                             (struct mesh *const[5]){ solid, panes, frame, lights, carpet });
            pcg_room_row_params_init(&rp->row);
            rp->row.label = row_room_number;
            rp->row.room.visit = room_parity_visit;
            rp->row.room.visit_priv = rp;

            CERR_RET(ref_rooms_for_each(variant, row_parity_ring, rp), return EXIT_FAILURE);
            if (!room_parity_ok(rp, sink, &frames[fi], "room row"))     return EXIT_FAILURE;
        }

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Atrium
 ****************************************************************************/

/* the flights' concrete, a finish of its own as the reference building gives them */
static const pcg_material smooth = { .name = "smooth", .uv_mode = PCG_UV_METERS };
static const pcg_palette atrium_palette = {
    .mat[PCG_ROLE_STRUCTURE]    = &concrete,
    .mat[PCG_ROLE_FLOOR]        = &wood,
    .mat[PCG_ROLE_CLADDING]     = &panel,
    .mat[PCG_ROLE_RAIL]         = &slat,
    .mat[PCG_ROLE_METAL]        = &bronze,
    .mat[PCG_ROLE_GLASS]        = &glass,
    .mat[PCG_ROLE_CARPET]       = &carpet_tile,
    .mat[PCG_ROLE_FIXTURE]      = &glow,
    .mat[PCG_ROLE_DIFFUSER]     = &glow_grid,
    .mat[PCG_ROLE_SIGN]         = &decal,
};

#define ATRIUM_CLASSES_MAX  16
#define ATRIUM_FLOORS_MAX   8
#define ATRIUM_ROOMS_MAX    128

/*
 * struct atrium_class - what an atrium emitted into the buckets of one
 * material and collide class, over every chunk
 */
struct atrium_class {
    const pcg_material  *mat;
    size_t              nr_tris;
    vec3                lo;
    vec3                hi;
    uint8_t             collide;
};

/* everything an atrium emitted, by class, and the rooms its visit hook saw */
struct atrium_walk {
    struct atrium_class cls[ATRIUM_CLASSES_MAX];
    size_t              nr_cls;
    unsigned int        chunk_lo;
    unsigned int        chunk_hi;
    size_t              nr_meshes;
    int                 visits;
    int                 visits_per_floor[ATRIUM_FLOORS_MAX];
    bool                seen[ATRIUM_FLOORS_MAX][ATRIUM_ROOMS_MAX];
    bool                visits_ok;
    bool                meshes_ok;
};

static cerr atrium_walk_fn(const pcg_key *key, struct mesh *mesh, void *priv)
{
    struct atrium_walk *w = priv;
    struct atrium_class *c = nullptr;

    for (size_t i = 0; i < w->nr_cls; i++)
        if (w->cls[i].mat == key->mat && w->cls[i].collide == key->collide)     c = &w->cls[i];

    if (!c) {
        if (w->nr_cls == ATRIUM_CLASSES_MAX)    return CERR_TOO_LARGE;
        c = &w->cls[w->nr_cls++];
        *c = (struct atrium_class){
            .mat = key->mat, .collide = key->collide,
            .lo = { INFINITY, INFINITY, INFINITY }, .hi = { -INFINITY, -INFINITY, -INFINITY },
        };
    }

    if (!w->nr_meshes || key->chunk < w->chunk_lo)  w->chunk_lo = key->chunk;
    if (!w->nr_meshes || key->chunk > w->chunk_hi)  w->chunk_hi = key->chunk;
    w->nr_meshes++;
    if (!sink_mesh_ok(mesh))    w->meshes_ok = false;

    c->nr_tris += mesh_nr_vx(mesh) / 3;
    for (size_t v = 0; v < mesh_nr_vx(mesh); v++)
        for (int k = 0; k < 3; k++) {
            float x = mesh_vx(mesh)[v * 3 + k];

            c->lo[k] = fminf(c->lo[k], x);
            c->hi[k] = fmaxf(c->hi[k], x);
        }

    return CERR_OK;
}

static const struct atrium_class *atrium_class_get(const struct atrium_walk *w, const pcg_material *mat,
                                                   pcg_collide collide)
{
    for (size_t i = 0; i < w->nr_cls; i++)
        if (w->cls[i].mat == mat && w->cls[i].collide == collide)   return &w->cls[i];

    return nullptr;
}

/* every room once: its floor and number unseen so far */
static cerr atrium_visit(const pcg_ctx *ctx, const pcg_room_info *info, void *priv)
{
    struct atrium_walk *w = priv;
    const pcg_room_tag *t = &info->tag;

    w->visits++;
    if (t->floor < 0 || t->floor >= ATRIUM_FLOORS_MAX || t->number < 1 || t->number >= ATRIUM_ROOMS_MAX ||
        w->seen[t->floor][t->number] || !(info->w > 0.0f) || !(info->d > 0.0f)) {
        w->visits_ok = false;
        return CERR_OK;
    }
    w->seen[t->floor][t->number] = true;
    w->visits_per_floor[t->floor]++;

    return CERR_OK;
}

/* the reference building's inner rooms drop a different wall per segment and floor */
static void atrium_carousel(const pcg_room_tag *tag, void *priv, pcg_room_entry *room, pcg_room_params *p)
{
    uint32_t side = (uint32_t)(tag->segment + tag->floor) & 3u;

    room->wall_mask &= ~(PCG_ROOM_SOLID(side) | PCG_ROOM_GLASS(side));
}

/* the rooms the carousel picks, by floor and number, in the order it picks them */
struct atrium_picks {
    int     room[256];
    size_t  nr;
    bool    overflow;
};

static void atrium_carousel_picks(const pcg_room_tag *tag, void *priv, pcg_room_entry *room, pcg_room_params *p)
{
    struct atrium_picks *picks = priv;

    if (picks->nr < array_size(picks->room))    picks->room[picks->nr++] = tag->floor * 1000 + tag->number;
    else                                        picks->overflow = true;
    atrium_carousel(tag, nullptr, room, p);
}

/*
 * The reference building with the carousel: the library's reference
 * params with the carousel on the inner ring, the flights' concrete in a
 * finish of its own; @rooms and @rings hold the carousel's copy of the
 * room tables
 */
struct atrium_case {
    pcg_atrium_params   p;
    pcg_room_entry      inner[8];
    pcg_atrium_ring     rings[2];
    pcg_atrium_floor    floor;
};

static bool atrium_case_init(struct atrium_case *c, bool carousel)
{
    pcg_atrium_params_init(&c->p);
    for (size_t i = 0; i < PCG_STAIR_NR_SURF; i++)  c->p.flight.stair.surf[i].mat = &smooth;
    c->p.flight.arrive.mat = &smooth;
    if (!carousel)  return true;

    const pcg_atrium_floor *ref = &c->p.room_floors[0];
    if (c->p.nr_room_floors != 1 || ref->nr_rings != 2 || ref->rings[1].nr_rooms > array_size(c->inner))
        return false;

    c->rings[0] = ref->rings[0];
    c->rings[1] = ref->rings[1];
    for (size_t k = 0; k < c->rings[1].nr_rooms; k++) {
        c->inner[k] = c->rings[1].rooms[k];
        c->inner[k].fn = atrium_carousel;
    }
    c->rings[1].rooms = c->inner;
    c->floor = (pcg_atrium_floor){ .rings = c->rings, .nr_rings = 2 };
    c->p.room_floors = &c->floor;

    return true;
}

/* emit @p at @f into a fresh @sink, walk it into @w; the visit hook counts into @w */
static cerr atrium_walk_get(pcg_atrium_params *p, const pcg_frame *f, float headroom, unsigned int chunk,
                            pcg_sink *sink, struct atrium_walk *w)
{
    *w = (struct atrium_walk){ .meshes_ok = true, .visits_ok = true };
    p->rooms.room.visit = atrium_visit;
    p->rooms.room.visit_priv = w;

    pcg_ctx ctx = { .sink = sink, .palette = &atrium_palette, .frame = *f, .chunk = chunk };
    CERR_RET_CERR(pcg_atrium_emit(&ctx, p, .frame = PCG_FRAME_IDENTITY, .headroom = headroom));

    return pcg_sink_for_each_mesh(sink, atrium_walk_fn, w);
}

/* the label centers' height range */
struct atrium_labels {
    size_t  nr;
    float   lo;
    float   hi;
};

static cerr atrium_label_fn(const pcg_label *label, void *priv)
{
    struct atrium_labels *l = priv;

    if (!l->nr || label->frame.org[1] < l->lo)  l->lo = label->frame.org[1];
    if (!l->nr || label->frame.org[1] > l->hi)  l->hi = label->frame.org[1];
    l->nr++;

    return CERR_OK;
}

/*
 * What the reference building with the carousel emits, by material and
 * collide class: its triangle count, and the bounding box of all of it
 * with the building's origin at 0
 */
static const struct {
    const pcg_material  *mat;
    pcg_collide         collide;
    size_t              nr_tris;
    vec3                lo;
    vec3                hi;
} atrium_expect[] = {
    { &concrete, PCG_COLLIDE_SOLID, 460,
      { -60.00000f, -0.80000f, -51.96152f }, { 60.00000f, 24.00000f, 51.96152f } },
    { &smooth, PCG_COLLIDE_SOLID, 1836,
      { -28.00000f, -0.17000f, -17.32051f }, { 17.76805f, 18.05000f, 20.78461f } },
    { &wood, PCG_COLLIDE_SOLID, 148,
      { -60.00000f, 0.00000f, -51.96152f }, { 60.00000f, 18.05000f, 51.96152f } },
    { &slat, PCG_COLLIDE_HAZARD, 39720,
      { -25.40077f, 0.05756f, -24.38871f }, { 28.12124f, 19.10138f, 24.38871f } },
    { &panel, PCG_COLLIDE_SOLID, 17592,
      { -59.96825f, 0.05000f, -51.96153f }, { 59.96825f, 23.98000f, 51.96153f } },
    { &bronze, PCG_COLLIDE_SOLID, 1068,
      { -28.28867f, 24.00000f, -24.49871f }, { 28.28867f, 24.50000f, 24.49871f } },
    { &bronze, PCG_COLLIDE_NONE, 9456,
      { -50.14969f, 0.05000f, -47.61153f }, { 50.14968f, 23.70000f, 47.61153f } },
    { &glass, PCG_COLLIDE_SOLID, 1072,
      { -59.87299f, 0.05000f, -51.85153f }, { 59.87299f, 23.70000f, 51.85153f } },
    { &carpet_tile, PCG_COLLIDE_NONE, 1380,
      { -53.64843f, 0.05000f, -51.65152f }, { 53.64843f, 18.06200f, 51.65152f } },
    { &glow, PCG_COLLIDE_NONE, 5520,
      { -47.63177f, 5.60000f, -49.80153f }, { 47.63177f, 23.65000f, 49.80153f } },
    { &glow_grid, PCG_COLLIDE_NONE, 4,
      { -28.28867f, 24.62000f, -24.49871f }, { 28.28867f, 24.62000f, 24.49871f } },
};

/*
 * The reference building with the carousel: its triangle counts and
 * bounds by class; one chunk per level, on from the context's;
 * 97 signs with the carousel, each storey's sign plates 1.5 m up; every
 * room visited once, 25 on the ground floor around the entrance and 30
 * on every other; and every room signed without the carousel.
 */
static int atrium_test(void)
{
    LOCAL_SET(void, buf) = mem_alloc(sizeof(struct atrium_walk));
    struct atrium_walk *w = buf;
    struct atrium_case c;
    if (!w || !atrium_case_init(&c, true))  return EXIT_FAILURE;

    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("atrium"), return EXIT_FAILURE);
    CERR_RET(atrium_walk_get(&c.p, &PCG_FRAME_IDENTITY, 3.2f, 7, sink, w), return EXIT_FAILURE);

    if (!w->meshes_ok || w->chunk_lo != 7 || w->chunk_hi != 7 + 4)  return EXIT_FAILURE;
    if (w->nr_cls != array_size(atrium_expect))                     return EXIT_FAILURE;
    for (size_t i = 0; i < array_size(atrium_expect); i++) {
        const struct atrium_class *ac = atrium_class_get(w, atrium_expect[i].mat, atrium_expect[i].collide);

        if (!ac || ac->nr_tris != atrium_expect[i].nr_tris)     return EXIT_FAILURE;
        if (!vec3_near(ac->lo, atrium_expect[i].lo, 1e-4f) || !vec3_near(ac->hi, atrium_expect[i].hi, 1e-4f))
            return EXIT_FAILURE;
    }

    struct atrium_labels labels = {};
    CERR_RET(pcg_sink_for_each_label(sink, atrium_label_fn, &labels), return EXIT_FAILURE);
    if (labels.nr != 97 || !near(labels.lo, 0.05f + 1.5f, 1e-5f) || !near(labels.hi, 18.05f + 1.5f, 1e-5f))
        return EXIT_FAILURE;

    if (!w->visits_ok || w->visits != 115)                          return EXIT_FAILURE;
    for (int f = 0; f < 4; f++)
        if (w->visits_per_floor[f] != (f ? 30 : 25))                return EXIT_FAILURE;

    /* the library's reference building signs every room */
    struct atrium_case plain;
    if (!atrium_case_init(&plain, false))   return EXIT_FAILURE;
    LOCAL_SET(pcg_sink, ps) = CRES_RET(pcg_sink_new("atrium"), return EXIT_FAILURE);
    CERR_RET(atrium_walk_get(&plain.p, &PCG_FRAME_IDENTITY, 3.2f, 0, ps, w), return EXIT_FAILURE);
    if (pcg_sink_get_stats(ps).nr_labels != 115 || w->visits != 115 || w->chunk_lo || w->chunk_hi != 4)
        return EXIT_FAILURE;

    /* the same building under a yawed and a mirrored frame */
    for (int m = 0; m < 2; m++) {
        pcg_frame f;
        test_frame(&f, 0.9f + m, m, 30.0f, 2.0f, -12.0f);

        LOCAL_SET(void, fbuf) = mem_alloc(sizeof(struct atrium_walk));
        struct atrium_walk *fw = fbuf;
        if (!fw)    return EXIT_FAILURE;

        LOCAL_SET(pcg_sink, fs) = CRES_RET(pcg_sink_new("atrium"), return EXIT_FAILURE);
        CERR_RET(atrium_walk_get(&c.p, &f, 3.2f, 7, fs, fw), return EXIT_FAILURE);
        if (!fw->meshes_ok || fw->nr_cls != array_size(atrium_expect) || fw->visits != 115) return EXIT_FAILURE;
        for (size_t i = 0; i < array_size(atrium_expect); i++) {
            const struct atrium_class *ac = atrium_class_get(fw, atrium_expect[i].mat, atrium_expect[i].collide);

            if (!ac || ac->nr_tris != atrium_expect[i].nr_tris)     return EXIT_FAILURE;
        }
        if (pcg_sink_get_stats(fs).nr_labels != 97)                 return EXIT_FAILURE;
    }

    /*
     * every one of the 69 inner rooms picked twice, the same rooms in the
     * same order: once for the checks, once to emit them
     */
    struct atrium_case pc;
    struct atrium_picks picks = {};
    if (!atrium_case_init(&pc, true))   return EXIT_FAILURE;
    for (size_t k = 0; k < pc.rings[1].nr_rooms; k++) {
        pc.inner[k].fn = atrium_carousel_picks;
        pc.inner[k].priv = &picks;
    }
    LOCAL_SET(pcg_sink, ks) = CRES_RET(pcg_sink_new("atrium"), return EXIT_FAILURE);
    CERR_RET(atrium_walk_get(&pc.p, &PCG_FRAME_IDENTITY, 3.2f, 0, ks, w), return EXIT_FAILURE);
    if (picks.overflow || picks.nr != 2 * 69)                               return EXIT_FAILURE;
    if (memcmp(picks.room, picks.room + 69, 69 * sizeof(picks.room[0])))    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/*
 * A walker needing more headroom keeps the galleries' rails and fascias
 * clear of the arriving flights for longer: fewer slats, less fascia
 */
static int atrium_headroom_test(void)
{
    LOCAL_SET(void, buf) = mem_alloc(sizeof(struct atrium_walk));
    struct atrium_walk *w = buf;
    struct atrium_case c;
    if (!w || !atrium_case_init(&c, true))  return EXIT_FAILURE;

    const float headroom[] = { 0.0f, 2.0f, 3.2f, 4.4f };
    size_t rail_prev = 0, fascia_prev = 0;

    for (size_t i = 0; i < array_size(headroom); i++) {
        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("atrium"), return EXIT_FAILURE);
        CERR_RET(atrium_walk_get(&c.p, &PCG_FRAME_IDENTITY, headroom[i], 0, sink, w), return EXIT_FAILURE);

        const struct atrium_class *rail = atrium_class_get(w, &slat, PCG_COLLIDE_HAZARD);
        const struct atrium_class *fascia = atrium_class_get(w, &concrete, PCG_COLLIDE_SOLID);
        if (!rail || !fascia)                                                       return EXIT_FAILURE;
        if (i && (!(rail->nr_tris < rail_prev) || fascia->nr_tris > fascia_prev))   return EXIT_FAILURE;

        rail_prev = rail->nr_tris;
        fascia_prev = fascia->nr_tris;
    }

    return EXIT_SUCCESS;
}

/* an atrium change that must fail */
typedef void (*atrium_break_fn)(pcg_atrium_params *p);

static void atrium_no_floors(pcg_atrium_params *p)        { p->floors = 1; }
static void atrium_no_sides(pcg_atrium_params *p)         { p->well_sides = 2; }
static void atrium_nan_floor_h(pcg_atrium_params *p)      { p->floor_h = NAN; }
static void atrium_inf_radius(pcg_atrium_params *p)       { p->radius = INFINITY; }
static void atrium_no_well(pcg_atrium_params *p)          { p->well_r = nullptr; }
static const float atrium_wide_r[] = { 20.0f, 61.0f }, atrium_zero_r[] = { 20.0f, 0.0f };
static void atrium_wide_well(pcg_atrium_params *p)        { p->well_r = atrium_wide_r; p->nr_well_r = 2; }
static void atrium_zero_well(pcg_atrium_params *p)        { p->well_r = atrium_zero_r; p->nr_well_r = 2; }
static void atrium_far_entrance(pcg_atrium_params *p)     { p->entrance = 6; }
static void atrium_no_entrance_w(pcg_atrium_params *p)    { p->entrance_w = 0.0f; }
static void atrium_neg_overlap(pcg_atrium_params *p)      { p->wall_overlap = -0.1f; }
static void atrium_no_room_floors(pcg_atrium_params *p)   { p->room_floors = nullptr; }
static void atrium_short_storey(pcg_atrium_params *p)     { p->flight.stair.going = 0.5f; }
static void atrium_bad_rail(pcg_atrium_params *p)         { p->gallery.rail.h = NAN; }
static void atrium_bad_fascia(pcg_atrium_params *p)       { p->gallery.fascia.depth = -INFINITY; }
static void atrium_bad_ground(pcg_atrium_params *p)       { p->ground.t = 0.0f; }
static void atrium_bad_roof(pcg_atrium_params *p)         { p->roof.finish_t = NAN; }
static void atrium_bad_column(pcg_atrium_params *p)       { p->column.w = INFINITY; }
static void atrium_bad_skylight(pcg_atrium_params *p)     { p->skylight.pitch = 0.01f; }
static void atrium_bad_facade(pcg_atrium_params *p)       { p->facade.wall.head = 6.0f; }
static void atrium_bad_door(pcg_atrium_params *p)         { p->rooms.room.door_w = 50.0f; }
static void atrium_low_ceiling(pcg_atrium_params *p)      { p->rooms.room.light.drop = 10.0f; }
static void atrium_many_segments(pcg_atrium_params *p)    { p->rooms.room.light.segments = 6000; }

static const pcg_atrium_ring atrium_deep_ring = { .rooms = nullptr, .nr_rooms = 0, .inset = 60.0f };
static const pcg_atrium_floor atrium_deep_floor = { .rings = &atrium_deep_ring, .nr_rings = 1 };
static void atrium_deep_ring_floor(pcg_atrium_params *p)  { p->room_floors = &atrium_deep_floor; }

static const pcg_atrium_ring atrium_nan_ring = { .rooms = nullptr, .nr_rooms = 0, .inset = NAN };
static const pcg_atrium_floor atrium_nan_floor = { .rings = &atrium_nan_ring, .nr_rings = 1 };
static void atrium_nan_ring_floor(pcg_atrium_params *p)   { p->room_floors = &atrium_nan_floor; }

static const pcg_atrium_ring atrium_lost_ring = { .rooms = nullptr, .nr_rooms = 2, .inset = 0.0f };
static const pcg_atrium_floor atrium_lost_floor = { .rings = &atrium_lost_ring, .nr_rings = 1 };
static void atrium_lost_rooms(pcg_atrium_params *p)       { p->room_floors = &atrium_lost_floor; }

static cerr atrium_count_visit(const pcg_ctx *ctx, const pcg_room_info *info, void *priv)
{
    (*(int *)priv)++;

    return CERR_OK;
}

/* emitting @p into a fresh sink fails and leaves it empty, without a visit */
static bool atrium_fails(pcg_atrium_params *p, const pcg_atrium_site *site, unsigned int chunk)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("atrium"), return false);
    int visits = 0;

    p->rooms.room.visit = atrium_count_visit;
    p->rooms.room.visit_priv = &visits;

    pcg_ctx ctx = { .sink = sink, .palette = &atrium_palette, .frame = PCG_FRAME_IDENTITY, .chunk = chunk };
    if (!failed(_pcg_atrium_emit(&ctx, p, site)))   return false;

    pcg_sink_stats st = pcg_sink_get_stats(sink);
    return !st.nr_models && !st.nr_labels && !visits;
}

/* bad style, fits and sites fail before anything is emitted */
static int atrium_bad_test(void)
{
    const atrium_break_fn breaks[] = {
        atrium_no_floors, atrium_no_sides, atrium_nan_floor_h, atrium_inf_radius, atrium_no_well,
        atrium_wide_well, atrium_zero_well, atrium_far_entrance, atrium_no_entrance_w, atrium_neg_overlap,
        atrium_no_room_floors, atrium_short_storey, atrium_bad_rail, atrium_bad_fascia, atrium_bad_ground,
        atrium_bad_roof, atrium_bad_column, atrium_bad_skylight, atrium_bad_facade, atrium_bad_door,
        atrium_low_ceiling, atrium_many_segments, atrium_deep_ring_floor, atrium_nan_ring_floor, atrium_lost_rooms,
    };
    const pcg_atrium_site site = { .frame = PCG_FRAME_IDENTITY, .headroom = 3.2f };
    pcg_atrium_params p;

    /* the reference building emits */
    pcg_atrium_params_init(&p);
    {
        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("atrium"), return EXIT_FAILURE);
        pcg_ctx ctx = { .sink = sink, .palette = &atrium_palette, .frame = PCG_FRAME_IDENTITY };
        CERR_RET(_pcg_atrium_emit(&ctx, &p, &site), return EXIT_FAILURE);
        if (!pcg_sink_get_stats(sink).nr_models)    return EXIT_FAILURE;
    }

    for (size_t i = 0; i < array_size(breaks); i++) {
        pcg_atrium_params_init(&p);
        breaks[i](&p);
        if (!atrium_fails(&p, &site, 0))    return EXIT_FAILURE;
    }

    /* nothing set, and missing arguments */
    p = (pcg_atrium_params){};
    if (!atrium_fails(&p, &site, 0))        return EXIT_FAILURE;

    pcg_atrium_params_init(&p);
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("atrium"), return EXIT_FAILURE);
    pcg_ctx ctx = { .sink = sink, .palette = &atrium_palette, .frame = PCG_FRAME_IDENTITY };
    if (!failed(_pcg_atrium_emit(nullptr, &p, &site)) || !failed(_pcg_atrium_emit(&ctx, nullptr, &site)) ||
        !failed(_pcg_atrium_emit(&ctx, &p, nullptr)))
        return EXIT_FAILURE;

    /* bad headroom, and levels past the last chunk */
    const float bad_headroom[] = { -1.0f, NAN, INFINITY, -INFINITY };
    for (size_t i = 0; i < array_size(bad_headroom); i++)
        if (!atrium_fails(&p, &(const pcg_atrium_site){ .frame = PCG_FRAME_IDENTITY, .headroom = bad_headroom[i] },
                          0))
            return EXIT_FAILURE;
    if (!atrium_fails(&p, &site, UINT_MAX - 2))     return EXIT_FAILURE;
    for (size_t i = 0; i < array_size(bad_frames); i++)
        if (!atrium_fails(&p, &(const pcg_atrium_site){ .frame = bad_frames[i], .headroom = 3.2f }, 0))
            return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Furniture
 ****************************************************************************/

static const pcg_material furn_plastic = { .name = "plastic", .uv_mode = PCG_UV_METERS };
static const pcg_material furn_atlas = { .name = "atlas", .uv_mode = PCG_UV_UNIT };
static const pcg_material furn_cork = { .name = "cork", .uv_mode = PCG_UV_UNIT };
static const pcg_palette furniture_palette = {
    .mat[PCG_ROLE_FURNITURE]    = &furn_plastic,
    .mat[PCG_ROLE_SCREEN]       = &furn_atlas,
    .mat[PCG_ROLE_BOARD]        = &furn_cork,
};

enum furn_kind {
    FURN_DESK,
    FURN_CHAIR,
    FURN_MONITOR,
    FURN_CORKBOARD,
    FURN_NR,
};

static const char *const furn_name[FURN_NR] = { "desk", "chair", "monitor", "corkboard" };

/* one piece of furniture at a frame */
struct furn_case {
    union {
        pcg_desk_params         desk;
        pcg_chair_params        chair;
        pcg_monitor_params      monitor;
        pcg_corkboard_params    board;
    };
    enum furn_kind  kind;
    pcg_frame       frame;
};

static void furn_case_init(struct furn_case *c, enum furn_kind kind)
{
    *c = (struct furn_case){ .kind = kind, .frame = { .org = { 2.0f, 0.0f, -1.0f }, .dir = { 0.6f, 0.8f } } };

    switch (kind) {
        case FURN_DESK:         pcg_desk_params_init(&c->desk); break;
        case FURN_CHAIR:        pcg_chair_params_init(&c->chair); break;
        case FURN_MONITOR:      pcg_monitor_params_init(&c->monitor); break;
        default:                pcg_corkboard_params_init(&c->board); break;
    }
}

static cerr furn_case_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct furn_case *c = priv;

    switch (c->kind) {
        case FURN_DESK:         return pcg_desk_emit(ctx, &c->desk, .frame = c->frame);
        case FURN_CHAIR:        return pcg_chair_emit(ctx, &c->chair, .frame = c->frame);
        case FURN_MONITOR:      return pcg_monitor_emit(ctx, &c->monitor, .frame = c->frame);
        default:                return pcg_corkboard_emit(ctx, &c->board, .frame = c->frame);
    }
}

static bool furn_fails(const struct furn_case *c)
{
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("furniture"), return false);
    pcg_ctx ctx = { .sink = sink, .palette = &furniture_palette, .frame = PCG_FRAME_IDENTITY };
    struct sink_walk w;

    return failed_inval(furn_case_emit(&ctx, c)) && sink_walk_get(sink, &w) && !w.nr;
}

/* the furthest a regular @n-gon of circumradius @r reaches along +c, its vertex 0 on +a */
static float ngon_reach_c(int n, float r)
{
    float c = 0.0f;

    for (int k = 0; k < n; k++) c = fmaxf(c, r * (float)sin(M_PI * 2 * k / n));

    return c;
}

/* what a piece's solid parts enclose, and their bounds in its frame */
static double furn_case_volume(const struct furn_case *c, vec3 lo, vec3 hi)
{
    switch (c->kind) {
        case FURN_DESK: {
            const pcg_desk_params *p = &c->desk;

            vec3_dup(lo, (vec3){ -p->w * 0.5f, 0.0f, -p->d * 0.5f });
            vec3_dup(hi, (vec3){ p->w * 0.5f, p->h, p->d * 0.5f });
            return (double)p->w * p->d * p->top_t + 2.0 * p->side_w * (p->h - p->top_t) * p->side_d;
        }
        case FURN_CHAIR: {
            const pcg_chair_params *p = &c->chair;
            float s0 = p->seat_c - p->seat_d * 0.5f, s1 = p->seat_c + p->seat_d * 0.5f;
            float stem = p->seat_h - p->seat_t - p->base_h;

            vec3_dup(lo, (vec3){ -p->base_r, 0.0f, s0 - p->back_t });
            vec3_dup(hi, (vec3){ p->base_r, p->seat_h + p->back_h, fmaxf(s1, ngon_reach_c(p->segments, p->base_r)) });
            return (double)p->seat_w * p->seat_d * p->seat_t + (double)p->back_w * p->back_h * p->back_t +
                   ngon_area(p->segments, p->stem_r) * stem + ngon_area(p->segments, p->base_r) * p->base_h;
        }
        case FURN_MONITOR: {
            const pcg_monitor_params *p = &c->monitor;

            vec3_dup(lo, (vec3){ -p->w * 0.5f, 0.0f,
                                 fminf(p->foot_c - p->foot_d * 0.5f, fminf(p->neck_c - p->neck_d * 0.5f,
                                                                           p->front - p->t)) });
            vec3_dup(hi, (vec3){ p->w * 0.5f, p->lift + p->h,
                                 fmaxf(p->foot_c + p->foot_d * 0.5f, p->front + p->screen_off) });
            return (double)p->foot_w * p->foot_d * p->foot_t + (double)p->neck_w * p->neck_d * (p->neck_h - p->foot_t) +
                   (double)p->w * p->h * p->t;
        }
        default: {
            const pcg_corkboard_params *p = &c->board;

            vec3_dup(lo, (vec3){ -p->w * 0.5f, p->center_y - p->h * 0.5f, p->standoff });
            vec3_dup(hi, (vec3){ p->w * 0.5f, p->center_y + p->h * 0.5f, p->standoff + p->t });
            return (double)p->w * p->h * p->t;
        }
    }
}

static int furniture_test(void)
{
    /* vertices of each piece: boxes of 36, the chair's two 10-sided cylinders, the screen's quad */
    static const size_t nr_vx[FURN_NR] = { 108, 72 + 2 * 120, 108 + 6, 36 };

    for (int k = 0; k < FURN_NR; k++) {
        struct furn_case c;
        furn_case_init(&c, k);

        /* the screen to a finish of its own, so the monitor's solid parts can be measured alone */
        if (k == FURN_MONITOR)  c.monitor.screen.mat = &decal;

        LOCAL_SET(pcg_sink, sink) = CRES_RET(element_sink(furn_case_emit, &c, &furniture_palette,
                                                          &PCG_FRAME_IDENTITY),
                                             return EXIT_FAILURE);
        struct sink_walk w;
        if (!sink_walk_get(sink, &w) || w.nr != (k == FURN_MONITOR ? 2 : 1))    return EXIT_FAILURE;

        size_t nr = 0;
        for (size_t b = 0; b < w.nr; b++) {
            if (w.key[b].collide != PCG_COLLIDE_NONE)       return EXIT_FAILURE;
            nr += mesh_nr_vx(w.mesh[b]);
        }
        if (nr != nr_vx[k])                                 return EXIT_FAILURE;

        const pcg_material *own[FURN_NR] = { &furn_plastic, &furn_plastic, &furn_atlas, &furn_cork };
        if (w.key[0].mat != own[k])                         return EXIT_FAILURE;

        /* closed and outward, over the measured bounds, the screen aside */
        vec3 lo, hi;
        double vol = furn_case_volume(&c, lo, hi);
        struct sink_walk solid = w;
        solid.nr = 1;
        if (!walk_closed(&solid) || fabs(walk_volume(&solid) - vol) > 1e-4 * vol) {
            fprintf(stderr, "%s: volume %f, expected %f\n", furn_name[k], walk_volume(&solid), vol);
            return EXIT_FAILURE;
        }
        if (!walk_aabb_is(&w, &c.frame, lo, hi))            return EXIT_FAILURE;

        if (!element_frames_ok(furn_case_emit, &c, &furniture_palette, k != FURN_MONITOR))  return EXIT_FAILURE;
    }

    /* the default finishes: plastic shells, an atlas monitor, a board, none colliding */
    struct furn_case d, h, m, b;
    furn_case_init(&d, FURN_DESK);
    furn_case_init(&h, FURN_CHAIR);
    furn_case_init(&m, FURN_MONITOR);
    furn_case_init(&b, FURN_CORKBOARD);
    const pcg_surface *surfs[] = { &d.desk.shell, &h.chair.shell, &h.chair.base, &m.monitor.shell, &m.monitor.screen,
                                   &b.board.board };
    const pcg_role roles[] = {
        PCG_ROLE_FURNITURE, PCG_ROLE_FURNITURE, PCG_ROLE_FURNITURE, PCG_ROLE_SCREEN, PCG_ROLE_SCREEN, PCG_ROLE_BOARD,
    };
    for (size_t i = 0; i < array_size(surfs); i++)
        if (surfs[i]->role != roles[i] || surfs[i]->collide != PCG_COLLIDE_NONE || surfs[i]->mat)   return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int furniture_surface_test(void)
{
    /* the pedestal on a finish of its own: both cylinders, and nothing else */
    struct furn_case c;
    furn_case_init(&c, FURN_CHAIR);
    c.chair.base.mat = &surf_mat[0];
    LOCAL_SET(pcg_sink, cs) = CRES_RET(element_sink(furn_case_emit, &c, &furniture_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    struct sink_walk w;
    if (!sink_walk_get(cs, &w) || w.nr != 2)                                    return EXIT_FAILURE;
    struct mesh *base = walk_mesh(&w, &surf_mat[0]), *shell = walk_mesh(&w, &furn_plastic);
    if (!base || !shell || mesh_nr_vx(base) != 240 || mesh_nr_vx(shell) != 72)              return EXIT_FAILURE;
    if (!mesh_y_within(base, &PCG_FRAME_IDENTITY, 0.0f, c.chair.seat_h - c.chair.seat_t))   return EXIT_FAILURE;

    /* the screen faces the front under any frame, mirrored or not */
    furn_case_init(&c, FURN_MONITOR);
    c.monitor.screen.mat = &decal;
    for (int i = 0; i < 4; i++) {
        test_frame(&c.frame, 1.3f * i - 0.4f, i & 1, 1.0f * i, 0.72f, -2.0f * i);
        LOCAL_SET(pcg_sink, ms) = CRES_RET(element_sink(furn_case_emit, &c, &furniture_palette,
                                                        &PCG_FRAME_IDENTITY),
                                           return EXIT_FAILURE);
        if (!sink_walk_get(ms, &w) || w.nr != 2)                                return EXIT_FAILURE;
        struct mesh *screen = walk_mesh(&w, &decal);
        if (!screen || mesh_nr_vx(screen) != 6)                                 return EXIT_FAILURE;
        if (!mesh_normals_along(screen, &c.frame, box_face_dir[4]))             return EXIT_FAILURE;
    }

    /* so does the board's front: 6 of its vertices face out of the wall */
    furn_case_init(&c, FURN_CORKBOARD);
    LOCAL_SET(pcg_sink, bs) = CRES_RET(element_sink(furn_case_emit, &c, &furniture_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(bs, &w) || w.nr != 1)                                    return EXIT_FAILURE;
    for (int k = 0; k < 6; k++)
        if (mesh_count_normals(w.mesh[0], &c.frame, box_face_dir[k]) != 6)      return EXIT_FAILURE;

    /* skipped surfaces leave their parts out */
    for (int k = 0; k < FURN_NR; k++) {
        furn_case_init(&c, k);
        pcg_surface *s = k == FURN_DESK ? c.desk.surf : k == FURN_CHAIR ? c.chair.surf :
                         k == FURN_MONITOR ? c.monitor.surf : c.board.surf;
        size_t nr_surf = k == FURN_DESK ? PCG_DESK_NR_SURF : k == FURN_CHAIR ? PCG_CHAIR_NR_SURF :
                         k == FURN_MONITOR ? PCG_MONITOR_NR_SURF : PCG_CORKBOARD_NR_SURF;

        for (size_t i = 0; i < nr_surf; i++)    s[i].skip = true;
        LOCAL_SET(pcg_sink, ss) = CRES_RET(element_sink(furn_case_emit, &c, &furniture_palette,
                                                        &PCG_FRAME_IDENTITY),
                                           return EXIT_FAILURE);
        if (!sink_walk_get(ss, &w) || w.nr)                                     return EXIT_FAILURE;
    }

    /* a shell can't wear an atlas, and a board can't wear meters */
    LOCAL_SET(pcg_sink, es) = CRES_RET(pcg_sink_new("furniture"), return EXIT_FAILURE);
    pcg_ctx ec = { .sink = es, .palette = &furniture_palette, .frame = PCG_FRAME_IDENTITY };
    furn_case_init(&c, FURN_DESK);
    c.desk.shell.mat = &furn_atlas;
    if (!failed(furn_case_emit(&ec, &c)))                                       return EXIT_FAILURE;
    furn_case_init(&c, FURN_CORKBOARD);
    c.board.board.mat = &furn_plastic;
    if (!failed(furn_case_emit(&ec, &c)))                                       return EXIT_FAILURE;
    furn_case_init(&c, FURN_MONITOR);
    c.monitor.shell.mat = &furn_plastic;
    if (!failed(furn_case_emit(&ec, &c)))                                       return EXIT_FAILURE;
    if (!sink_walk_get(es, &w) || w.nr)                                         return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* the float fields of @c's params, nullptr-terminated */
static void furn_case_fields(struct furn_case *c, float **f)
{
    size_t n = 0;

    switch (c->kind) {
        case FURN_DESK: {
            pcg_desk_params *p = &c->desk;
            float *x[] = { &p->w, &p->d, &p->h, &p->top_t, &p->side_w, &p->side_inset, &p->side_d };
            for (size_t i = 0; i < array_size(x); i++)  f[n++] = x[i];
            break;
        }
        case FURN_CHAIR: {
            pcg_chair_params *p = &c->chair;
            float *x[] = {
                &p->seat_w, &p->seat_d, &p->seat_h, &p->seat_t, &p->seat_c, &p->back_w, &p->back_h, &p->back_t,
                &p->stem_r, &p->base_r, &p->base_h,
            };
            for (size_t i = 0; i < array_size(x); i++)  f[n++] = x[i];
            break;
        }
        case FURN_MONITOR: {
            pcg_monitor_params *p = &c->monitor;
            float *x[] = {
                &p->w, &p->h, &p->t, &p->lift, &p->front, &p->bezel, &p->screen_off, &p->foot_w, &p->foot_d,
                &p->foot_t, &p->foot_c, &p->neck_w, &p->neck_d, &p->neck_h, &p->neck_c,
            };
            for (size_t i = 0; i < array_size(x); i++)  f[n++] = x[i];
            for (int i = 0; i < 4; i++) {
                f[n++] = &p->shell_uv[i];
                f[n++] = &p->screen_uv[i];
            }
            break;
        }
        default: {
            pcg_corkboard_params *p = &c->board;
            float *x[] = { &p->w, &p->h, &p->t, &p->center_y, &p->standoff };
            for (size_t i = 0; i < array_size(x); i++)  f[n++] = x[i];
            for (int i = 0; i < 4; i++) {
                f[n++] = &p->face_uv[i];
                f[n++] = &p->rim_uv[i];
            }
            break;
        }
    }
    f[n] = nullptr;
}

#define FURN_FIELDS_MAX 32

static int furniture_bad_test(void)
{
    const float nonfinite[] = { INFINITY, -INFINITY, NAN };
    struct furn_case c, bad;

    for (int k = 0; k < FURN_NR; k++) {
        furn_case_init(&c, k);

        /* zeroed params */
        bad = c;
        memset(&bad.desk, 0, sizeof(bad) - offsetof(struct furn_case, desk));
        if (!furn_fails(&bad))                                  return EXIT_FAILURE;

        /* every float, infinite or NaN; and the first three, sizes all, negative */
        float *fields[FURN_FIELDS_MAX];
        bad = c;
        furn_case_fields(&bad, fields);
        for (size_t f = 0; fields[f]; f++) {
            for (size_t i = 0; i < array_size(nonfinite); i++) {
                bad = c;
                *fields[f] = nonfinite[i];
                if (!furn_fails(&bad)) {
                    fprintf(stderr, "%s: field %zu = %f accepted\n", furn_name[k], f, nonfinite[i]);
                    return EXIT_FAILURE;
                }
            }
            bad = c;
            *fields[f] = -0.1f;
            if (f < 3 && !furn_fails(&bad))                     return EXIT_FAILURE;
        }

        for (size_t i = 0; i < array_size(bad_frames); i++) {
            bad = c;
            bad.frame = bad_frames[i];
            if (!furn_fails(&bad))                              return EXIT_FAILURE;
        }
    }

    /* fits */
    furn_case_init(&c, FURN_DESK);
    bad = c;
    bad.desk.top_t = c.desk.h;
    if (!furn_fails(&bad))                                      return EXIT_FAILURE;
    bad = c;
    bad.desk.side_d = c.desk.d + 0.01f;
    if (!furn_fails(&bad))                                      return EXIT_FAILURE;
    bad = c;
    bad.desk.side_w = c.desk.w * 0.5f;
    if (!furn_fails(&bad))                                      return EXIT_FAILURE;
    bad = c;
    bad.desk.side_inset = -0.01f;
    if (!furn_fails(&bad))                                      return EXIT_FAILURE;

    furn_case_init(&c, FURN_CHAIR);
    bad = c;
    bad.chair.segments = 2;
    if (!furn_fails(&bad))                                      return EXIT_FAILURE;
    bad = c;
    bad.chair.seat_h = c.chair.seat_t + c.chair.base_h;
    if (!furn_fails(&bad))                                      return EXIT_FAILURE;
    /* more segments than a mesh holds, also where segments * 12 wraps a 32-bit size_t: the check says so */
    const int many[] = { PCG_CYLINDER_SIDES_MAX + 1, 357913942 };
    for (size_t k = 0; k < array_size(many); k++) {
        bad = c;
        bad.chair.segments = many[k];
        cerr err = pcg_chair_check(&bad.chair, &(pcg_chair_site){ .frame = bad.frame });
        if (!IS_CERR_CODE(err, CERR_TOO_LARGE))                 return EXIT_FAILURE;
    }

    furn_case_init(&c, FURN_MONITOR);
    bad = c;
    bad.monitor.bezel = c.monitor.h * 0.5f;
    if (!furn_fails(&bad))                                      return EXIT_FAILURE;
    bad = c;
    bad.monitor.neck_h = c.monitor.foot_t;
    if (!furn_fails(&bad))                                      return EXIT_FAILURE;
    bad = c;
    bad.monitor.screen_off = 0.0f;
    if (!furn_fails(&bad))                                      return EXIT_FAILURE;
    bad = c;
    bad.monitor.lift = -0.01f;
    if (!furn_fails(&bad))                                      return EXIT_FAILURE;

    furn_case_init(&c, FURN_CORKBOARD);
    bad = c;
    bad.board.center_y = c.board.h * 0.4f;
    if (!furn_fails(&bad))                                      return EXIT_FAILURE;
    bad = c;
    bad.board.standoff = -0.01f;
    if (!furn_fails(&bad))                                      return EXIT_FAILURE;

    /* missing arguments */
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("furniture"), return EXIT_FAILURE);
    pcg_ctx ctx = { .sink = sink, .palette = &furniture_palette, .frame = PCG_FRAME_IDENTITY };
    furn_case_init(&c, FURN_DESK);
    if (!failed_inval(_pcg_desk_emit(nullptr, &c.desk, &(pcg_desk_site){ .frame = c.frame })))  return EXIT_FAILURE;
    if (!failed_inval(_pcg_desk_emit(&ctx, nullptr, &(pcg_desk_site){ .frame = c.frame })))     return EXIT_FAILURE;
    if (!failed_inval(_pcg_desk_emit(&ctx, &c.desk, nullptr)))                                  return EXIT_FAILURE;
    if (!failed_inval(_pcg_chair_emit(&ctx, nullptr, nullptr)))                                 return EXIT_FAILURE;
    if (!failed_inval(_pcg_monitor_emit(&ctx, nullptr, nullptr)))                               return EXIT_FAILURE;
    if (!failed_inval(_pcg_corkboard_emit(&ctx, nullptr, nullptr)))                             return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/*
 * Reference copies of the original furniture emitters, each in its own
 * space: the desk, the chair and the corkboard face +z, the computer's
 * screen too, with a texture density of 1 repeat per meter.
 */
static void ref_furn_face(struct mesh *m, vec3 c00, vec3 c10, vec3 c11, vec3 c01,
                          float u0, float v0, float u1, float v1)
{
    prim_emit_opts opts = { .mesh = m };
    vec2 t0[3] = { { u0, v0 }, { u0, v1 }, { u1, v0 } };
    vec2 t1[3] = { { u0, v1 }, { u1, v1 }, { u1, v0 } };

    opts.uv = t0;
    _prim_emit_triangle3(c00, c01, c10, &opts);
    opts.uv = t1;
    _prim_emit_triangle3(c01, c11, c10, &opts);
}

static cerr ref_furn_box(struct mesh *m, vec3 lo, vec3 hi)
{
    CRES_RET_CERR(_prim_begin(36, &(prim_emit_opts){ .mesh = m, .tangents = true }));

    float x0 = lo[0], y0 = lo[1], z0 = lo[2];
    float x1 = hi[0], y1 = hi[1], z1 = hi[2];

    ref_furn_face(m, (vec3){ x1, y0, z0 }, (vec3){ x1, y0, z1 }, (vec3){ x1, y1, z1 },
                  (vec3){ x1, y1, z0 }, z0, y0, z1, y1);
    ref_furn_face(m, (vec3){ x0, y0, z1 }, (vec3){ x0, y0, z0 }, (vec3){ x0, y1, z0 },
                  (vec3){ x0, y1, z1 }, -z1, y0, -z0, y1);
    ref_furn_face(m, (vec3){ x0, y1, z0 }, (vec3){ x1, y1, z0 }, (vec3){ x1, y1, z1 },
                  (vec3){ x0, y1, z1 }, x0, z0, x1, z1);
    ref_furn_face(m, (vec3){ x0, y0, z0 }, (vec3){ x0, y0, z1 }, (vec3){ x1, y0, z1 },
                  (vec3){ x1, y0, z0 }, z0, x0, z1, x1);
    ref_furn_face(m, (vec3){ x1, y0, z1 }, (vec3){ x0, y0, z1 }, (vec3){ x0, y1, z1 },
                  (vec3){ x1, y1, z1 }, -x1, y0, -x0, y1);
    ref_furn_face(m, (vec3){ x0, y0, z0 }, (vec3){ x1, y0, z0 }, (vec3){ x1, y1, z0 },
                  (vec3){ x0, y1, z0 }, x0, y0, x1, y1);

    return CERR_OK;
}

static cerr ref_furn_box_uv(struct mesh *m, vec3 lo, vec3 hi, float u0, float v0, float u1, float v1)
{
    CRES_RET_CERR(_prim_begin(36, &(prim_emit_opts){ .mesh = m, .tangents = true }));

    float x0 = lo[0], y0 = lo[1], z0 = lo[2];
    float x1 = hi[0], y1 = hi[1], z1 = hi[2];

    ref_furn_face(m, (vec3){ x1, y0, z0 }, (vec3){ x1, y0, z1 }, (vec3){ x1, y1, z1 },
                  (vec3){ x1, y1, z0 }, u0, v0, u1, v1);
    ref_furn_face(m, (vec3){ x0, y0, z1 }, (vec3){ x0, y0, z0 }, (vec3){ x0, y1, z0 },
                  (vec3){ x0, y1, z1 }, u0, v0, u1, v1);
    ref_furn_face(m, (vec3){ x0, y1, z0 }, (vec3){ x1, y1, z0 }, (vec3){ x1, y1, z1 },
                  (vec3){ x0, y1, z1 }, u0, v0, u1, v1);
    ref_furn_face(m, (vec3){ x0, y0, z0 }, (vec3){ x0, y0, z1 }, (vec3){ x1, y0, z1 },
                  (vec3){ x1, y0, z0 }, u0, v0, u1, v1);
    ref_furn_face(m, (vec3){ x1, y0, z1 }, (vec3){ x0, y0, z1 }, (vec3){ x0, y1, z1 },
                  (vec3){ x1, y1, z1 }, u0, v0, u1, v1);
    ref_furn_face(m, (vec3){ x0, y0, z0 }, (vec3){ x1, y0, z0 }, (vec3){ x1, y1, z0 },
                  (vec3){ x0, y1, z0 }, u0, v0, u1, v1);

    return CERR_OK;
}

static cerr ref_furn_desk(struct mesh *m)
{
    CERR_RET_CERR(ref_furn_box(m, (vec3){ -0.75f, 0.66f, -0.35f }, (vec3){ 0.75f, 0.72f, 0.35f }));
    CERR_RET_CERR(ref_furn_box(m, (vec3){ -0.73f, 0.0f, -0.31f }, (vec3){ -0.61f, 0.66f, 0.31f }));
    return ref_furn_box(m, (vec3){ 0.61f, 0.0f, -0.31f }, (vec3){ 0.73f, 0.66f, 0.31f });
}

/* the chair's seat and back in @shell, its pedestal in @base */
static cerr ref_furn_chair(struct mesh *shell, struct mesh *base)
{
    CERR_RET_CERR(ref_furn_box(shell, (vec3){ -0.22f, 0.42f, -0.20f }, (vec3){ 0.22f, 0.48f, 0.24f }));
    CERR_RET_CERR(ref_furn_box(shell, (vec3){ -0.21f, 0.48f, -0.26f }, (vec3){ 0.21f, 0.88f, -0.20f }));
    CERR_RET_CERR(_prim_emit_cylinder((vec3){ 0.0f, 0.05f, 0.0f }, 0.37f, 0.035f, 10,
                                      &(prim_emit_opts){ .mesh = base, .tangents = true }));
    return _prim_emit_cylinder((vec3){ 0.0f, 0.0f, 0.0f }, 0.05f, 0.24f, 10,
                               &(prim_emit_opts){ .mesh = base, .tangents = true });
}

/* the computer's shell in @shell, its screen overlay in @screen */
static cerr ref_furn_computer(struct mesh *shell, struct mesh *screen)
{
    CERR_RET_CERR(ref_furn_box_uv(shell, (vec3){ -0.11f, 0.0f, -0.10f }, (vec3){ 0.11f, 0.02f, 0.06f },
                                  0.05f, 0.05f, 0.40f, 0.40f));
    CERR_RET_CERR(ref_furn_box_uv(shell, (vec3){ -0.025f, 0.02f, -0.06f }, (vec3){ 0.025f, 0.14f, -0.02f },
                                  0.05f, 0.05f, 0.40f, 0.40f));
    CERR_RET_CERR(ref_furn_box_uv(shell, (vec3){ -0.27f, 0.10f, -0.045f }, (vec3){ 0.27f, 0.46f, 0.005f },
                                  0.05f, 0.05f, 0.40f, 0.40f));
    CRES_RET_CERR(_prim_begin(6, &(prim_emit_opts){ .mesh = screen, .tangents = true }));
    ref_furn_face(screen, (vec3){ -0.245f, 0.125f, 0.008f }, (vec3){ 0.245f, 0.125f, 0.008f },
                  (vec3){ 0.245f, 0.435f, 0.008f }, (vec3){ -0.245f, 0.435f, 0.008f },
                  0.98f, 0.95f, 0.52f, 0.05f);

    return CERR_OK;
}

/* the corkboard, centered on its origin, its back at z = 0 */
static cerr ref_furn_corkboard(struct mesh *m)
{
    CRES_RET_CERR(_prim_begin(36, &(prim_emit_opts){ .mesh = m, .tangents = true }));

    float x0 = -0.48f, x1 = 0.48f, y0 = -0.34f, y1 = 0.34f, z0 = 0.0f, z1 = 0.035f;

    ref_furn_face(m, (vec3){ x0, y0, z1 }, (vec3){ x1, y0, z1 }, (vec3){ x1, y1, z1 },
                  (vec3){ x0, y1, z1 }, 0.0f, 1.0f, 1.0f, 0.0f);
    ref_furn_face(m, (vec3){ x1, y0, z0 }, (vec3){ x0, y0, z0 }, (vec3){ x0, y1, z0 },
                  (vec3){ x1, y1, z0 }, 0.005f, 0.4f, 0.01f, 0.6f);
    ref_furn_face(m, (vec3){ x1, y0, z1 }, (vec3){ x1, y0, z0 }, (vec3){ x1, y1, z0 },
                  (vec3){ x1, y1, z1 }, 0.005f, 0.4f, 0.01f, 0.6f);
    ref_furn_face(m, (vec3){ x0, y0, z0 }, (vec3){ x0, y0, z1 }, (vec3){ x0, y1, z1 },
                  (vec3){ x0, y1, z0 }, 0.005f, 0.4f, 0.01f, 0.6f);
    ref_furn_face(m, (vec3){ x0, y1, z1 }, (vec3){ x1, y1, z1 }, (vec3){ x1, y1, z0 },
                  (vec3){ x0, y1, z0 }, 0.005f, 0.4f, 0.01f, 0.6f);
    ref_furn_face(m, (vec3){ x0, y0, z0 }, (vec3){ x1, y0, z0 }, (vec3){ x1, y0, z1 },
                  (vec3){ x0, y0, z1 }, 0.005f, 0.4f, 0.01f, 0.6f);

    return CERR_OK;
}

/*
 * @m's triangles turned over: the same corners on the same texture
 * coordinates, wound the other way. The original corkboard and screen
 * overlay wound their faces into the board and the panel, where culling
 * hid them; the elements wind them out.
 */
static cresp(mesh) ref_turned_over(struct mesh *m)
{
    size_t nr = mesh_nr_vx(m);
    LOCAL_SET(mesh_t, out) = CRES_RET_T(ref_new_checked(mesh, .name = "turned"), mesh);

    CRES_RET_T(_prim_begin(nr, &(prim_emit_opts){ .mesh = out, .tangents = true }), mesh);
    for (size_t v = 0; v + 2 < nr; v += 3) {
        const float *p = &mesh_vx(m)[v * 3], *t = &mesh_tx(m)[v * 2];
        vec2 uv[3] = { { t[0], t[1] }, { t[4], t[5] }, { t[2], t[3] } };

        _prim_emit_triangle3((float *)&p[0], (float *)&p[6], (float *)&p[3],
                             &(prim_emit_opts){ .mesh = out, .uv = uv });
    }

    return cresp_val(mesh, NOCU(out));
}

/* the furniture parity cases' finishes: each part in a bucket of its own */
static const pcg_material furn_pedestal = { .name = "pedestal", .uv_mode = PCG_UV_METERS };

static int furniture_parity_frame(const pcg_frame *frame)
{
    LOCAL_SET(mesh_t, desk) = CRES_RET(ref_new_checked(mesh, .name = "desk"), return EXIT_FAILURE);
    LOCAL_SET(mesh_t, chair) = CRES_RET(ref_new_checked(mesh, .name = "chair"), return EXIT_FAILURE);
    LOCAL_SET(mesh_t, pedestal) = CRES_RET(ref_new_checked(mesh, .name = "pedestal"), return EXIT_FAILURE);
    LOCAL_SET(mesh_t, computer) = CRES_RET(ref_new_checked(mesh, .name = "computer"), return EXIT_FAILURE);
    LOCAL_SET(mesh_t, overlay) = CRES_RET(ref_new_checked(mesh, .name = "overlay"), return EXIT_FAILURE);
    LOCAL_SET(mesh_t, board) = CRES_RET(ref_new_checked(mesh, .name = "board"), return EXIT_FAILURE);
    if (failed(ref_furn_desk(desk)) || failed(ref_furn_chair(chair, pedestal)) ||
        failed(ref_furn_computer(computer, overlay)) || failed(ref_furn_corkboard(board)))
        return EXIT_FAILURE;
    LOCAL_SET(mesh_t, screen) = CRES_RET(ref_turned_over(overlay), return EXIT_FAILURE);
    LOCAL_SET(mesh_t, cork) = CRES_RET(ref_turned_over(board), return EXIT_FAILURE);

    struct furn_case c;
    struct sink_walk w;

    /* the desk: run-local meters */
    furn_case_init(&c, FURN_DESK);
    c.frame = *frame;
    LOCAL_SET(pcg_sink, ds) = CRES_RET(element_sink(furn_case_emit, &c, &furniture_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(ds, &w) || w.nr != 1)                                            return EXIT_FAILURE;
    if (!parity_check("desk", w.mesh[0], desk, frame, false, 1.0f))                     return EXIT_FAILURE;

    /* the chair: its boxes in full, its pedestal in shape (meter charts on the cylinders) */
    furn_case_init(&c, FURN_CHAIR);
    c.frame = *frame;
    c.chair.base.mat = &furn_pedestal;
    LOCAL_SET(pcg_sink, cs) = CRES_RET(element_sink(furn_case_emit, &c, &furniture_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(cs, &w) || w.nr != 2)                                                    return EXIT_FAILURE;
    if (!parity_check("chair", walk_mesh(&w, &furn_plastic), chair, frame, false, 1.0f))        return EXIT_FAILURE;
    if (!parity_check_shape("chair pedestal", walk_mesh(&w, &furn_pedestal), pedestal, frame))  return EXIT_FAILURE;

    /* the monitor: its shell on the atlas cell, the screen over the screen cell, facing out */
    furn_case_init(&c, FURN_MONITOR);
    c.frame = *frame;
    c.monitor.screen.mat = &decal;
    LOCAL_SET(pcg_sink, ms) = CRES_RET(element_sink(furn_case_emit, &c, &furniture_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(ms, &w) || w.nr != 2)                                                return EXIT_FAILURE;
    if (!parity_check("monitor", walk_mesh(&w, &furn_atlas), computer, frame, false, 1.0f)) return EXIT_FAILURE;
    if (!parity_check("monitor screen", walk_mesh(&w, &decal), screen, frame, false, 1.0f)) return EXIT_FAILURE;

    /* the corkboard, from the wall's foot: the original's origin is the board's center on the wall */
    furn_case_init(&c, FURN_CORKBOARD);
    c.frame = *frame;
    LOCAL_SET(pcg_sink, bs) = CRES_RET(element_sink(furn_case_emit, &c, &furniture_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(bs, &w) || w.nr != 1)                                            return EXIT_FAILURE;
    const pcg_frame hang = { .org = { 0.0f, c.board.center_y, c.board.standoff }, .dir = { 1.0f, 0.0f } };
    pcg_frame at;
    pcg_frame_compose(frame, &hang, &at);
    if (!parity_check("corkboard", w.mesh[0], cork, &at, false, 1.0f))  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int furniture_parity_test(void)
{
    pcg_frame f;

    if (furniture_parity_frame(&PCG_FRAME_IDENTITY))    return EXIT_FAILURE;

    /* a wall's yaw, off the origin, and the desk top's height */
    test_frame(&f, 2.4f, false, 37.0f, 18.05f, -21.0f);
    if (furniture_parity_frame(&f))                     return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/****************************************************************************
 * Office
 ****************************************************************************/

/* the furnishing with the desk and the chair each in a bucket of their own */
static void office_params_split(pcg_office_params *p)
{
    pcg_office_params_init(p);
    p->desk.shell.mat = &surf_mat[0];
    p->chair.shell.mat = &surf_mat[1];
    p->chair.base.mat = &surf_mat[1];
}

/* furnish @info, drawing from a stream seeded with @seed */
static cerr office_furnish_seeded(const pcg_ctx *ctx, const pcg_office_params *p, const pcg_room_info *info,
                                  uint64_t seed)
{
    LOCAL_SET(prng, rng) = CRES_RET_CERR(prng_new(PRNG_DEFAULT, seed));
    pcg_ctx c = *ctx;
    c.rng = rng;

    return pcg_office_furnish(&c, info, (void *)p);
}

/* a bucket's bounds in @f's local coordinates; false if @w has none */
static bool office_piece(const struct sink_walk *w, const pcg_material *mat, const pcg_frame *f, vec3 lo, vec3 hi)
{
    struct sink_walk one = {};

    for (size_t i = 0; i < w->nr; i++)
        if (w->key[i].mat == mat) {
            one.key[one.nr] = w->key[i];
            one.mesh[one.nr++] = w->mesh[i];
        }
    if (!one.nr)    return false;

    walk_aabb(&one, f, lo, hi);

    return true;
}

/* how far a box [@lo, @hi] in the room's (a, c) keeps off each side of a @w by @d interior */
static void office_side_gaps(const vec3 lo, const vec3 hi, float w, float d, float gap[PCG_ROOM_NR_SIDES])
{
    gap[PCG_ROOM_FRONT] = d - hi[2];
    gap[PCG_ROOM_BACK] = lo[2];
    gap[PCG_ROOM_LEFT] = lo[0];
    gap[PCG_ROOM_RIGHT] = w - hi[0];
}

/* the bounds of @mat's vertices from height @y0 up to @y1, in world coordinates; false if there are none */
static bool office_band(const struct sink_walk *w, const pcg_material *mat, float y0, float y1, vec3 lo, vec3 hi)
{
    bool found = false;

    for (size_t i = 0; i < w->nr; i++) {
        if (w->key[i].mat != mat)   continue;

        for (size_t v = 0; v < mesh_nr_vx(w->mesh[i]); v++) {
            const float *p = &mesh_vx(w->mesh[i])[v * 3];

            if (p[1] < y0 || p[1] > y1)     continue;
            for (int k = 0; k < 3; k++) {
                lo[k] = found ? fminf(lo[k], p[k]) : p[k];
                hi[k] = found ? fmaxf(hi[k], p[k]) : p[k];
            }
            found = true;
        }
    }

    return found;
}

/* how far the middle of a box [@lo, @hi] keeps off @side of a @w by @d interior */
static float office_middle_gap(const vec3 lo, const vec3 hi, float w, float d, int side)
{
    vec3 mid;
    float gap[PCG_ROOM_NR_SIDES];

    vec3_add(mid, lo, hi);
    vec3_scale(mid, mid, 0.5f);
    office_side_gaps(mid, mid, w, d, gap);

    return gap[side];
}

/* the sides the rules let the desk take for @mask, as a bit mask over &enum pcg_room_side */
static unsigned int office_desk_sides(uint32_t mask)
{
    unsigned int solid = 0, glass = 0;

    for (int side = 0; side < PCG_ROOM_NR_SIDES; side++) {
        if (mask & PCG_ROOM_DOOR(side))         continue;
        if (mask & PCG_ROOM_SOLID(side))        solid |= 1u << side;
        else if (mask & PCG_ROOM_GLASS(side))   glass |= 1u << side;
    }

    /* the back over the sides, solid over glass */
    unsigned int pick = solid ? solid : glass;

    return pick & (1u << PCG_ROOM_BACK) ? 1u << PCG_ROOM_BACK : pick;
}

/*
 * Every wall mask with every side bare, solid, glass, solid with a
 * doorway or glass with one, in a 3 m by 4 m room: the desk backs onto a
 * side the rules allow, its gap off that wall; the chair stands out in
 * front of its middle with its back away from it; the monitor looks into
 * the room within its slide of that middle; the corkboard hangs on the
 * wall over that middle if, and only if, the wall is solid; everything
 * stays inside the interior.
 */
static int office_test(void)
{
    pcg_office_params p;
    office_params_split(&p);

    static const uint32_t states[] = { 0, 1, 1u << PCG_ROOM_GLASS_BIT, 1 | (1u << PCG_ROOM_DOOR_BIT),
                                       (1u << PCG_ROOM_GLASS_BIT) | (1u << PCG_ROOM_DOOR_BIT) };
    size_t nr_furnished = 0, nr_boards = 0;

    for (int combo = 0; combo < 5 * 5 * 5 * 5; combo++) {
        uint32_t mask = 0;
        for (int side = 0, k = combo; side < PCG_ROOM_NR_SIDES; side++, k /= 5) mask |= states[k % 5] << side;

        const pcg_room_info info = { .w = 3.0f, .d = 4.0f, .h = 2.8f, .door_w = 1.1f, .door_h = 2.2f,
                                     .wall_mask = mask };
        LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("office"), return EXIT_FAILURE);
        pcg_ctx ctx = { .sink = sink, .palette = &furniture_palette, .frame = PCG_FRAME_IDENTITY };
        CERR_RET(office_furnish_seeded(&ctx, &p, &info, 0x5eedu + combo), return EXIT_FAILURE);

        struct sink_walk w;
        if (!sink_walk_get(sink, &w))                       return EXIT_FAILURE;

        unsigned int allowed = office_desk_sides(mask);
        if (!allowed) {
            if (w.nr)                                       return EXIT_FAILURE;
            continue;
        }
        nr_furnished++;

        /* the desk's back is the gap off exactly one side, and that side is allowed */
        vec3 lo, hi;
        float gap[PCG_ROOM_NR_SIDES];
        if (!office_piece(&w, &surf_mat[0], &PCG_FRAME_IDENTITY, lo, hi))   return EXIT_FAILURE;
        office_side_gaps(lo, hi, info.w, info.d, gap);
        int side = -1;
        for (int s = 0; s < PCG_ROOM_NR_SIDES; s++)
            if (near(gap[s], p.wall_gap, 1e-4f)) {
                if (side >= 0)                              return EXIT_FAILURE;
                side = s;
            }
        if (side < 0 || !(allowed & (1u << side))) {
            fprintf(stderr, "office: mask %#x, desk on side %d, allowed %#x\n", mask, side, allowed);
            return EXIT_FAILURE;
        }
        /* the desk's ends keep clear of the corners */
        for (int s = 0; s < PCG_ROOM_NR_SIDES; s++)
            if (s != side && gap[s] < p.corner_clear - 1e-4f)   return EXIT_FAILURE;

        /* the monitor stands on the desk */
        vec3 mlo, mhi;
        if (!office_piece(&w, &furn_atlas, &PCG_FRAME_IDENTITY, mlo, mhi))      return EXIT_FAILURE;
        if (!near(mlo[1], p.desk.h, 1e-5f) || !near(hi[1], p.desk.h, 1e-5f))    return EXIT_FAILURE;

        /* the chair stands in front of the desk: its middle past the desk's front */
        vec3 clo, chi;
        float cgap[PCG_ROOM_NR_SIDES];
        if (!office_piece(&w, &surf_mat[1], &PCG_FRAME_IDENTITY, clo, chi)) return EXIT_FAILURE;
        office_side_gaps(clo, chi, info.w, info.d, cgap);
        int across = side == PCG_ROOM_BACK || side == PCG_ROOM_FRONT ? 2 : 0;
        if (!(cgap[side] + (chi[across] - clo[across]) * 0.5f > p.wall_gap + p.desk.d)) return EXIT_FAILURE;

        /* the chair's pedestal straight out from the desk's middle, its back on the far side from the desk */
        int along = 2 - across;
        float mid = (lo[along] + hi[along]) * 0.5f;
        vec3 plo, phi, klo, khi;
        if (!office_band(&w, &surf_mat[1], 0.0f, p.chair.base_h, plo, phi))             return EXIT_FAILURE;
        if (!office_band(&w, &surf_mat[1], p.chair.seat_h + 1e-3f, info.h, klo, khi))   return EXIT_FAILURE;
        float pgap = office_middle_gap(plo, phi, info.w, info.d, side);
        float out = pgap - p.wall_gap - p.desk.d * 0.5f;
        if (!near((plo[along] + phi[along]) * 0.5f, mid, 1e-4f))                    return EXIT_FAILURE;
        if (out < p.chair_off - 1e-4f || out > p.chair_off + p.chair_slide + 1e-4f) return EXIT_FAILURE;
        if (!(office_middle_gap(klo, khi, info.w, info.d, side) > pgap))            return EXIT_FAILURE;

        /* the monitor within its slide of the desk's middle, its screen looking into the room */
        if (!(fabsf((mlo[along] + mhi[along]) * 0.5f - mid) <= p.monitor_slide * 0.5f + 1e-4f)) return EXIT_FAILURE;
        struct mesh *monitor = walk_mesh(&w, &furn_atlas);
        vec3 in = {};
        in[across] = side == PCG_ROOM_BACK || side == PCG_ROOM_LEFT ? 1.0f : -1.0f;
        float su0 = fminf(p.monitor.screen_uv[0], p.monitor.screen_uv[2]);
        size_t nr_screen = 0;
        for (size_t v = 0; v < mesh_nr_vx(monitor); v++) {
            if (mesh_tx(monitor)[v * 2] < su0)                      continue;
            if (!vec3_near(&mesh_norm(monitor)[v * 3], in, 1e-4f))  return EXIT_FAILURE;
            nr_screen++;
        }
        if (nr_screen != 6)                                 return EXIT_FAILURE;

        /* the corkboard on the desk's wall when it is solid, nowhere else */
        vec3 blo, bhi;
        float bgap[PCG_ROOM_NR_SIDES];
        bool board = office_piece(&w, &furn_cork, &PCG_FRAME_IDENTITY, blo, bhi);
        if (board != !!(mask & PCG_ROOM_SOLID(side)))       return EXIT_FAILURE;
        if (board) {
            nr_boards++;
            office_side_gaps(blo, bhi, info.w, info.d, bgap);
            if (!near(bgap[side], p.board.standoff, 1e-5f))             return EXIT_FAILURE;
            if (!near((blo[along] + bhi[along]) * 0.5f, mid, 1e-4f))    return EXIT_FAILURE;
            if (!near(blo[1], p.board.center_y - p.board.h * 0.5f, 1e-5f) ||
                !near(bhi[1], p.board.center_y + p.board.h * 0.5f, 1e-5f))
                return EXIT_FAILURE;
        }

        /* nothing outside the interior */
        walk_aabb(&w, &PCG_FRAME_IDENTITY, lo, hi);
        if (lo[0] < 0.0f || lo[2] < 0.0f || hi[0] > info.w || hi[2] > info.d)   return EXIT_FAILURE;
        if (lo[1] < 0.0f || hi[1] > info.h)                                     return EXIT_FAILURE;
        for (size_t b = 0; b < w.nr; b++)
            if (w.key[b].collide != PCG_COLLIDE_NONE)                           return EXIT_FAILURE;
    }

    /* every mask with a walled side without a doorway is furnished; solid desk walls get boards */
    if (nr_furnished != 5 * 5 * 5 * 5 - 3 * 3 * 3 * 3 || !nr_boards)    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int office_size_test(void)
{
    pcg_office_params p;
    office_params_split(&p);

    /* below the minimum either way: nothing; at it: furnished, inside the interior */
    const float sizes[][2] = { { 2.1f, 4.0f }, { 4.0f, 2.1f }, { 2.19f, 2.19f }, { 2.2f, 2.2f }, { 2.2f, 6.0f } };
    for (size_t i = 0; i < array_size(sizes); i++) {
        const pcg_room_info info = { .w = sizes[i][0], .d = sizes[i][1], .h = 2.8f,
                                     .wall_mask = PCG_ROOM_SOLID(PCG_ROOM_BACK) | PCG_ROOM_SOLID(PCG_ROOM_LEFT) };
        bool small = info.w < p.min_size || info.d < p.min_size;

        for (uint64_t seed = 1; seed < 40; seed++) {
            LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("office"), return EXIT_FAILURE);
            pcg_ctx ctx = { .sink = sink, .palette = &furniture_palette, .frame = PCG_FRAME_IDENTITY };
            CERR_RET(office_furnish_seeded(&ctx, &p, &info, seed), return EXIT_FAILURE);

            struct sink_walk w;
            if (!sink_walk_get(sink, &w) || (small ? w.nr != 0 : w.nr != 4))    return EXIT_FAILURE;
            if (small)                                                          continue;

            vec3 lo, hi;
            walk_aabb(&w, &PCG_FRAME_IDENTITY, lo, hi);
            if (lo[0] < 0.0f || lo[2] < 0.0f || hi[0] > info.w || hi[2] > info.d)   return EXIT_FAILURE;
        }
    }

    /* a wall too short to keep the desk off its corners takes it in the middle */
    pcg_office_params wide = p;
    wide.min_size = 1.0f;
    const pcg_room_info narrow = { .w = 1.9f, .d = 3.0f, .h = 2.8f, .wall_mask = PCG_ROOM_SOLID(PCG_ROOM_BACK) };
    LOCAL_SET(pcg_sink, ns) = CRES_RET(pcg_sink_new("office"), return EXIT_FAILURE);
    pcg_ctx nc = { .sink = ns, .palette = &furniture_palette, .frame = PCG_FRAME_IDENTITY };
    CERR_RET(office_furnish_seeded(&nc, &wide, &narrow, 7), return EXIT_FAILURE);
    struct sink_walk w;
    vec3 lo, hi;
    if (!sink_walk_get(ns, &w) || !office_piece(&w, &surf_mat[0], &PCG_FRAME_IDENTITY, lo, hi)) return EXIT_FAILURE;
    if (!near((lo[0] + hi[0]) * 0.5f, narrow.w * 0.5f, 1e-5f))                                  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* a row of offices, furnished from one stream in visit order */
struct office_row {
    const pcg_office_params *p;
    uint64_t                seed;
    int                     nr_rooms;
};

static cerr office_row_emit(const pcg_ctx *ctx, const void *priv)
{
    const struct office_row *row = priv;
    LOCAL_SET(prng, rng) = CRES_RET_CERR(prng_new(PRNG_DEFAULT, row->seed));
    pcg_ctx c = *ctx;
    c.rng = rng;

    for (int i = 0; i < row->nr_rooms; i++) {
        const pcg_room_info info = {
            .w = 3.0f + 0.5f * i, .d = 4.0f, .h = 2.8f,
            .wall_mask = PCG_ROOM_SOLID(PCG_ROOM_LEFT) | PCG_ROOM_SOLID(PCG_ROOM_RIGHT) |
                         PCG_ROOM_GLASS(PCG_ROOM_FRONT) | PCG_ROOM_DOOR(PCG_ROOM_FRONT),
        };
        const pcg_frame room = { .org = { 7.0f * i, 0.0f, 0.0f }, .dir = { 1.0f, 0.0f } };
        pcg_ctx at = pcg_ctx_at(&c, &room);

        CERR_RET_CERR(pcg_office_furnish(&at, &info, (void *)row->p));
    }

    return CERR_OK;
}

static int office_determinism_test(void)
{
    pcg_office_params p;
    office_params_split(&p);

    /*
     * One office under any frame. Further out, a 2 cm side of the chair's
     * stem turns by more than the check's 1e-4 over a last-bit difference
     * in its corners.
     */
    struct office_row row = { .p = &p, .seed = 0xa781, .nr_rooms = 1 };
    if (!element_frames_ok(office_row_emit, &row, &furniture_palette, false))   return EXIT_FAILURE;

    /* a row of them: one seed, the same bytes; another seed, other offices */
    row.nr_rooms = 6;
    LOCAL_SET(pcg_sink, r0) = CRES_RET(element_sink(office_row_emit, &row, &furniture_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    LOCAL_SET(pcg_sink, r1) = CRES_RET(element_sink(office_row_emit, &row, &furniture_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    struct sink_walk w0, w1;
    if (!sink_walk_get(r0, &w0) || !sink_walk_get(r1, &w1) || w0.nr != 4 || w1.nr != 4) return EXIT_FAILURE;
    for (size_t b = 0; b < w0.nr; b++)
        if (!meshes_same_bytes(w0.mesh[b], w1.mesh[b]))                                 return EXIT_FAILURE;

    LOCAL_SET(pcg_sink, s0) = CRES_RET(element_sink(office_row_emit, &row, &furniture_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    row.seed++;
    LOCAL_SET(pcg_sink, s1) = CRES_RET(element_sink(office_row_emit, &row, &furniture_palette, &PCG_FRAME_IDENTITY),
                                       return EXIT_FAILURE);
    if (!sink_walk_get(s0, &w0) || !sink_walk_get(s1, &w1) || w0.nr != 4 || w1.nr != 4) return EXIT_FAILURE;
    bool same = true;
    for (size_t b = 0; b < w0.nr; b++)  same = same && meshes_same_bytes(w0.mesh[b], w1.mesh[b]);
    if (same)                                                       return EXIT_FAILURE;

    /*
     * Every room splits a stream of its own off the context's, whatever
     * it then draws, or doesn't: a room after one that stays empty and a
     * room after one that is furnished come out the same
     */
    const pcg_room_info first[] = {
        { .w = 1.0f, .d = 1.0f, .h = 2.8f, .wall_mask = PCG_ROOM_SOLID(PCG_ROOM_BACK) },
        { .w = 3.0f, .d = 3.0f, .h = 2.8f },
        { .w = 5.0f, .d = 3.0f, .h = 2.8f, .wall_mask = PCG_ROOM_GLASS(PCG_ROOM_BACK) },
    };
    const pcg_room_info next = { .w = 3.0f, .d = 4.0f, .h = 2.8f, .wall_mask = PCG_ROOM_SOLID(PCG_ROOM_LEFT) };
    LOCAL_SET(pcg_sink, s2) = nullptr;

    for (size_t i = 0; i < array_size(first); i++) {
        LOCAL_SET(pcg_sink, before) = CRES_RET(pcg_sink_new("office"), return EXIT_FAILURE);
        LOCAL_SET(pcg_sink, s) = CRES_RET(pcg_sink_new("office"), return EXIT_FAILURE);
        LOCAL_SET(prng, rng) = CRES_RET(prng_new(PRNG_DEFAULT, 99), return EXIT_FAILURE);
        pcg_ctx cf = { .sink = before, .palette = &furniture_palette, .rng = rng, .frame = PCG_FRAME_IDENTITY };
        pcg_ctx cs = cf;
        cs.sink = s;

        CERR_RET(pcg_office_furnish(&cf, &first[i], &p), return EXIT_FAILURE);
        CERR_RET(pcg_office_furnish(&cs, &next, &p), return EXIT_FAILURE);
        if (!sink_walk_get(before, &w0) || w0.nr != (i == 2 ? 3 : 0))   return EXIT_FAILURE;
        if (!sink_walk_get(s, &w0) || w0.nr != 4)                       return EXIT_FAILURE;

        if (!s2) {
            s2 = NOCU(s);
            continue;
        }
        if (!sink_walk_get(s2, &w1))                                    return EXIT_FAILURE;
        for (size_t k = 0; k < w0.nr; k++)
            if (!meshes_same_bytes(w0.mesh[k], w1.mesh[k]))             return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/* the furnisher as a room's visit hook: everything inside the room's interior, in the site's frame */
static int office_room_test(void)
{
    pcg_office_params op;
    office_params_split(&op);

    pcg_room_params rp;
    pcg_room_params_init(&rp);
    rp.visit = pcg_office_furnish;
    rp.visit_priv = &op;

    pcg_palette pal = furniture_palette;
    pal.mat[PCG_ROLE_CLADDING] = &panel;
    pal.mat[PCG_ROLE_CARPET] = &carpet_tile;
    pal.mat[PCG_ROLE_FIXTURE] = &glow;

    pcg_frame f;
    test_frame(&f, 0.7f, true, 12.0f, 6.05f, -4.0f);
    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("office"), return EXIT_FAILURE);
    LOCAL_SET(prng, rng) = CRES_RET(prng_new(PRNG_DEFAULT, 3), return EXIT_FAILURE);
    pcg_ctx ctx = { .sink = sink, .palette = &pal, .rng = rng, .frame = PCG_FRAME_IDENTITY };
    CERR_RET(pcg_room_emit(&ctx, &rp, .frame = f, .a0 = 1.0f, .w = 4.0f, .d = 5.0f, .h = 3.0f, .ceiling = 2.9f,
                           .wall_mask = PCG_ROOM_SOLID(PCG_ROOM_BACK) | PCG_ROOM_SOLID(PCG_ROOM_LEFT) |
                                        PCG_ROOM_SOLID(PCG_ROOM_RIGHT)),
             return EXIT_FAILURE);

    struct sink_walk w;
    if (!sink_walk_get(sink, &w))                                   return EXIT_FAILURE;

    const pcg_material *pieces[] = { &surf_mat[0], &surf_mat[1], &furn_atlas, &furn_cork };
    float rt = rp.wall.t;
    for (size_t i = 0; i < array_size(pieces); i++) {
        vec3 lo, hi;

        if (!office_piece(&w, pieces[i], &f, lo, hi))               return EXIT_FAILURE;
        if (lo[0] < 1.0f + rt - 1e-4f || hi[0] > 5.0f - rt + 1e-4f) return EXIT_FAILURE;
        if (lo[2] < rt - 1e-4f || hi[2] > 5.0f - rt + 1e-4f)        return EXIT_FAILURE;
        if (lo[1] < -1e-4f || hi[1] > 2.9f)                         return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int office_bad_test(void)
{
    pcg_office_params p;
    pcg_office_params_init(&p);

    LOCAL_SET(pcg_sink, sink) = CRES_RET(pcg_sink_new("office"), return EXIT_FAILURE);
    LOCAL_SET(prng, rng) = CRES_RET(prng_new(PRNG_DEFAULT, 1), return EXIT_FAILURE);
    pcg_ctx ctx = { .sink = sink, .palette = &furniture_palette, .rng = rng, .frame = PCG_FRAME_IDENTITY };
    const pcg_room_info info = { .w = 3.0f, .d = 4.0f, .h = 2.8f, .wall_mask = PCG_ROOM_SOLID(PCG_ROOM_BACK) };

    /* no stream, no room, no params */
    pcg_ctx none = ctx;
    none.rng = nullptr;
    if (!failed_inval(pcg_office_furnish(&none, &info, &p)))        return EXIT_FAILURE;
    if (!failed_inval(pcg_office_furnish(nullptr, &info, &p)))      return EXIT_FAILURE;
    if (!failed_inval(pcg_office_furnish(&ctx, nullptr, &p)))       return EXIT_FAILURE;
    if (!failed_inval(pcg_office_furnish(&ctx, &info, nullptr)))    return EXIT_FAILURE;

    /* zeroed params, and every distance infinite, NaN or negative */
    pcg_office_params bad = {};
    if (!failed_inval(pcg_office_furnish(&ctx, &info, &bad)))       return EXIT_FAILURE;
    const float values[] = { INFINITY, -INFINITY, NAN, -0.1f };
    for (size_t i = 0; i < array_size(values); i++) {
        float *fields[] = {
            &bad.min_size, &bad.corner_clear, &bad.wall_gap, &bad.monitor_slide, &bad.chair_off, &bad.chair_slide,
            &bad.chair_turn,
        };

        for (size_t k = 0; k < array_size(fields); k++) {
            bad = p;
            *fields[k] = values[i];
            if (!failed_inval(pcg_office_furnish(&ctx, &info, &bad)))   return EXIT_FAILURE;
        }
    }
    bad = p;
    bad.luck = -1;
    if (!failed_inval(pcg_office_furnish(&ctx, &info, &bad)))       return EXIT_FAILURE;

    /* a weight past its range either way, where the sum of a wall's weights would overflow */
    const int weights[] = { PCG_OFFICE_WEIGHT_MAX + 1, INT_MAX, -PCG_OFFICE_WEIGHT_MAX - 1, INT_MIN };
    for (size_t i = 0; i < array_size(weights); i++) {
        int *fields[] = { &bad.solid_score, &bad.back_score, &bad.luck };

        for (size_t k = 0; k < array_size(fields); k++) {
            bad = p;
            *fields[k] = weights[i];
            if (!failed_inval(pcg_office_furnish(&ctx, &info, &bad)))   return EXIT_FAILURE;
        }
    }
    /* and right at its edge, a good one */
    bad = p;
    bad.solid_score = -PCG_OFFICE_WEIGHT_MAX;
    bad.back_score = PCG_OFFICE_WEIGHT_MAX;
    bad.luck = PCG_OFFICE_WEIGHT_MAX;
    CERR_RET(pcg_office_check(&bad), return EXIT_FAILURE);

    /* a bad piece fails the room before anything is placed */
    bad = p;
    bad.board.h = 0.0f;
    if (!failed_inval(pcg_office_furnish(&ctx, &info, &bad)))       return EXIT_FAILURE;
    bad = p;
    bad.chair.segments = 2;
    if (!failed_inval(pcg_office_furnish(&ctx, &info, &bad)))       return EXIT_FAILURE;
    bad = p;
    bad.chair.segments = 6000;
    cerr err = pcg_office_furnish(&ctx, &info, &bad);
    if (!IS_CERR_CODE(err, CERR_TOO_LARGE))                         return EXIT_FAILURE;
    bad = p;
    bad.monitor.bezel = 1.0f;
    if (!failed_inval(pcg_office_furnish(&ctx, &info, &bad)))       return EXIT_FAILURE;
    bad = p;
    bad.desk.top_t = bad.desk.h;
    if (!failed_inval(pcg_office_furnish(&ctx, &info, &bad)))       return EXIT_FAILURE;

    struct sink_walk w;
    if (!sink_walk_get(sink, &w) || w.nr)                           return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "frame point", .test = frame_point_test },
    { .name = "frame compose", .test = frame_compose_test },
    { .name = "frame run", .test = frame_run_test },
    { .name = "frame check", .test = frame_check_test },
    { .name = "span subtract", .test = span_subtract_test },
    { .name = "span region", .test = span_region_test },
    { .name = "span segment runs", .test = span_segment_runs_test },
    { .name = "span openings", .test = span_openings_test },
    { .name = "sink rollover", .test = sink_rollover_test },
    { .name = "sink key", .test = sink_key_test },
    { .name = "sink resolve", .test = sink_resolve_test },
    { .name = "sink label", .test = sink_label_test },
    { .name = "shape plate", .test = shape_plate_test },
    { .name = "shape plate skip", .test = shape_plate_skip_test },
    { .name = "shape plate fan", .test = shape_plate_fan_test },
    { .name = "shape plate sheet", .test = shape_plate_sheet_test },
    { .name = "shape plate chart", .test = shape_plate_chart_test },
    { .name = "shape band", .test = shape_band_test },
    { .name = "shape pane", .test = shape_pane_test },
    { .name = "shape rod", .test = shape_rod_test },
    { .name = "shape frames", .test = shape_frame_test },
    { .name = "shape box", .test = shape_box_test },
    { .name = "shape box faces", .test = shape_box_faces_test },
    { .name = "shape box rect", .test = shape_box_rect_test },
    { .name = "shape quad", .test = shape_quad_test },
    { .name = "shape cylinder", .test = shape_cylinder_test },
    { .name = "slab", .test = slab_test },
    { .name = "slab surfaces", .test = slab_surface_test },
    { .name = "slab parity", .test = slab_parity_test },
    { .name = "fascia", .test = fascia_test },
    { .name = "fascia surfaces", .test = fascia_surface_test },
    { .name = "fascia parity", .test = fascia_parity_test },
    { .name = "column", .test = column_test },
    { .name = "column surfaces", .test = column_surface_test },
    { .name = "column parity", .test = column_parity_test },
    { .name = "balustrade", .test = balustrade_test },
    { .name = "balustrade path", .test = balustrade_path_test },
    { .name = "balustrade surfaces", .test = balustrade_surface_test },
    { .name = "balustrade parity", .test = balustrade_parity_test },
    { .name = "stair steps", .test = stair_steps_test },
    { .name = "stair", .test = stair_test },
    { .name = "stair surfaces", .test = stair_surface_test },
    { .name = "stair parity", .test = stair_parity_test },
    { .name = "wall", .test = wall_test },
    { .name = "wall surfaces", .test = wall_surface_test },
    { .name = "wall parity", .test = wall_parity_test },
    { .name = "glazing", .test = glazing_test },
    { .name = "glazing surfaces", .test = glazing_surface_test },
    { .name = "glazing parity", .test = glazing_parity_test },
    { .name = "window wall", .test = window_wall_test },
    { .name = "window wall surfaces", .test = window_wall_surface_test },
    { .name = "window wall parity", .test = window_wall_parity_test },
    { .name = "fixture", .test = fixture_test },
    { .name = "fixture surfaces", .test = fixture_surface_test },
    { .name = "fixture parity", .test = fixture_parity_test },
    { .name = "sign", .test = sign_test },
    { .name = "sign surfaces", .test = sign_surface_test },
    { .name = "sign parity", .test = sign_parity_test },
    { .name = "skylight", .test = skylight_test },
    { .name = "skylight surfaces", .test = skylight_surface_test },
    { .name = "skylight parity", .test = skylight_parity_test },
    { .name = "flight", .test = flight_test },
    { .name = "flight cut", .test = flight_cut_test },
    { .name = "flight parity", .test = flight_parity_test },
    { .name = "gallery", .test = gallery_test },
    { .name = "gallery parity", .test = gallery_parity_test },
    { .name = "facade", .test = facade_test },
    { .name = "facade parity", .test = facade_parity_test },
    { .name = "room", .test = room_test },
    { .name = "room parity", .test = room_parity_test },
    { .name = "room row", .test = room_row_test },
    { .name = "room row fill", .test = room_row_fill_test },
    { .name = "room ring", .test = room_ring_test },
    { .name = "room row parity", .test = room_row_parity_test },
    { .name = "atrium", .test = atrium_test },
    { .name = "atrium headroom", .test = atrium_headroom_test },
    { .name = "atrium bad input", .test = atrium_bad_test },
    { .name = "furniture", .test = furniture_test },
    { .name = "furniture surfaces", .test = furniture_surface_test },
    { .name = "furniture bad input", .test = furniture_bad_test },
    { .name = "furniture parity", .test = furniture_parity_test },
    { .name = "office", .test = office_test },
    { .name = "office size", .test = office_size_test },
    { .name = "office determinism", .test = office_determinism_test },
    { .name = "office room", .test = office_room_test },
    { .name = "office bad input", .test = office_bad_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
