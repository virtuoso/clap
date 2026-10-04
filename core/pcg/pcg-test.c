// SPDX-License-Identifier: Apache-2.0
/*
 * Procedural content library tests, built as the standalone pcg_tests
 * binary: the pure pcg sources and the engine helpers they lean on, with
 * the shared test harness, no renderer.
 */
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mesh.h"
#include "poly2d.h"
#include "primitives.h"
#include "test.h"
#include "util.h"
#include "pcg/frame.h"
#include "pcg/material.h"
#include "pcg/sink.h"
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

    pcg_sink_stats st = pcg_sink_get_stats(sink);
    if (st.nr_labels != 1 || st.nr_models)                          return EXIT_FAILURE;

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
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
