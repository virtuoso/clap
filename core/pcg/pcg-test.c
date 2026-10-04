// SPDX-License-Identifier: Apache-2.0
/*
 * Procedural content library tests, built as the standalone pcg_tests
 * binary: the pure pcg sources and the engine helpers they lean on, with
 * the shared test harness, no renderer.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "poly2d.h"
#include "test.h"
#include "util.h"
#include "pcg/frame.h"
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

static const struct test tests[] = {
    { .name = "frame point", .test = frame_point_test },
    { .name = "frame compose", .test = frame_compose_test },
    { .name = "frame run", .test = frame_run_test },
    { .name = "frame check", .test = frame_check_test },
    { .name = "span subtract", .test = span_subtract_test },
    { .name = "span region", .test = span_region_test },
    { .name = "span segment runs", .test = span_segment_runs_test },
    { .name = "span openings", .test = span_openings_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
