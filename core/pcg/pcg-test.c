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
#include "primitives.h"
#include "test.h"
#include "util.h"
#include "pcg/frame.h"
#include "pcg/material.h"
#include "pcg/shape.h"
#include "pcg/sink.h"
#include "pcg/slab.h"
#include "pcg/span.h"

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
 * position, normal, tangent and effective texture coordinates (UV times
 * the material's density, which the originals baked into the vertices)
 */
#define PARITY_EPS  1e-5f

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

static bool parity_vx_near(const parity_vx *a, const parity_vx *b, parity_err *e)
{
    parity_err d = {
        .p  = max_diff(a->p, b->p, 3),
        .n  = max_diff(a->n, b->n, 3),
        .t  = max_diff(a->t, b->t, 4),
        .uv = max_diff(a->uv, b->uv, 2),
    };

    if (d.p > PARITY_EPS || d.n > PARITY_EPS || d.t > PARITY_EPS || d.uv > PARITY_EPS)    return false;

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
 * any order and start at any corner, but wind the same way.
 */
static bool parity_check(const char *what, struct mesh *m, struct mesh *ref, const pcg_frame *f, bool planar,
                         float density)
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
            for (int rot = 0; rot < 3 && !found; rot++) {
                parity_err e = err;

                found = parity_vx_near(&got[0], &exp[s * 3 + rot], &e) &&
                        parity_vx_near(&got[1], &exp[s * 3 + (rot + 1) % 3], &e) &&
                        parity_vx_near(&got[2], &exp[s * 3 + (rot + 2) % 3], &e);
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
            if (!parity_check("slab", walk_mesh(&w, &concrete), rs, f, true, REF_UV_CONCRETE))
                return EXIT_FAILURE;
            if (!finished)  continue;

            LOCAL_SET(mesh_t, rd) = CRES_RET(ref_new_checked(mesh, .name = "ref deck"), return EXIT_FAILURE);
            if (failed(ref_plate_emit(rd, 6, 60.0f, hs, cases[i].hole_r, 0.0f, c.p.finish_t, REF_UV_FLOOR,
                                      REF_PLATE_NO_BOTTOM)))
                return EXIT_FAILURE;
            if (!parity_check("deck", walk_mesh(&w, &wood), rd, f, true, REF_UV_FLOOR))
                return EXIT_FAILURE;
        }

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
    { .name = "shape band", .test = shape_band_test },
    { .name = "shape pane", .test = shape_pane_test },
    { .name = "shape rod", .test = shape_rod_test },
    { .name = "shape frames", .test = shape_frame_test },
    { .name = "slab", .test = slab_test },
    { .name = "slab surfaces", .test = slab_surface_test },
    { .name = "slab parity", .test = slab_parity_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
