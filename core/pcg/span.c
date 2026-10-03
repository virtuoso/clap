// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include "memory.h"
#include "poly2d.h"
#include "util.h"
#include "pcg/internal.h"
#include "pcg/span.h"

cerr pcg_openings_check(const pcg_opening *open, size_t nr, float len)
{
    if (nr && !open)    return CERR_INVALID_ARGUMENTS;

    float prev_hi = 0.0f;

    for (size_t i = 0; i < nr; i++) {
        pcg_span s = pcg_opening_span(&open[i]);

        if (!pcg_positive(open[i].w))
            return CERR_INVALID_ARGUMENTS_REASON(.fmt = "opening %zu has no width", .arg0 = (void *)(uintptr_t)i);
        if (!pcg_ordered(prev_hi, s.lo) || !pcg_ordered(s.hi, len))
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "opening %zu overlaps the previous one or reaches past the run",
                .arg0   = (void *)(uintptr_t)i
            );
        prev_hi = s.hi;
    }

    return CERR_OK;
}

/* @r's bounded ends are finite */
static bool range_finite(const pcg_range *r)
{
    return (r->open_lo || isfinite(r->lo)) && (r->open_hi || isfinite(r->hi));
}

cerr pcg_regions_check(const pcg_region *r, size_t nr)
{
    if (nr && !r)   return CERR_INVALID_ARGUMENTS;

    for (size_t i = 0; i < nr; i++) {
        CERR_RET_CERR(pcg_frame_check(&r[i].frame));
        if (!range_finite(&r[i].along) || !range_finite(&r[i].across))
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "region %zu has a bound that isn't finite",
                .arg0   = (void *)(uintptr_t)i
            );
    }

    return CERR_OK;
}

bool pcg_region_cut(const pcg_region *r, const vec2 a, const vec2 b, float min_frac, pcg_span *cut)
{
    const pcg_frame *f = &r->frame;
    vec2 across;

    pcg_frame_across(f, across);

    /* the frame origin's offsets along both axes */
    float oa = f->org[0] * f->dir[0] + f->org[2] * f->dir[1];
    float oc = f->org[0] * across[0] + f->org[2] * across[1];
    poly2d_halfplane hp[4];
    size_t nr = 0;

    if (!r->along.open_lo)  hp[nr++] = (poly2d_halfplane){ .n = { -f->dir[0], -f->dir[1] }, .d = -(oa + r->along.lo) };
    if (!r->along.open_hi)  hp[nr++] = (poly2d_halfplane){ .n = { f->dir[0], f->dir[1] }, .d = oa + r->along.hi };
    if (!r->across.open_lo) hp[nr++] = (poly2d_halfplane){ .n = { -across[0], -across[1] }, .d = -(oc + r->across.lo) };
    if (!r->across.open_hi) hp[nr++] = (poly2d_halfplane){ .n = { across[0], across[1] }, .d = oc + r->across.hi };

    float t0, t1;

    if (!poly2d_segment_clip(a, b, hp, nr, &t0, &t1))   return false;
    if (!(t1 - t0 > min_frac))                          return false;

    vec2 d;

    vec2_sub(d, (float *)b, (float *)a);
    float len = vec2_len(d);

    cut->lo = t0 * len;
    cut->hi = t1 * len;

    return true;
}

cres(size_t) pcg_span_subtract(const pcg_span *run, const pcg_span *cuts, size_t nr_cuts, float merge,
                               float min_run, pcg_span *out, size_t cap)
{
    if (!run || (nr_cuts && !cuts) || (cap && !out))    return cres_error(size_t, CERR_INVALID_ARGUMENTS);
    if (!pcg_ordered(run->lo, run->hi)) return cres_error(size_t, CERR_INVALID_ARGUMENTS, .fmt = "reversed run");
    if (!pcg_nonnegative(merge) || !pcg_nonnegative(min_run))
        return cres_error(size_t, CERR_INVALID_ARGUMENTS, .fmt = "negative merge or minimum run");

    for (size_t i = 0; i < nr_cuts; i++)
        if (!pcg_ordered(cuts[i].lo, cuts[i].hi))
            return cres_error(size_t, CERR_INVALID_ARGUMENTS, .fmt = "cut %zu is reversed",
                              .arg0 = (void *)(uintptr_t)i);

    LOCAL_SET(void, buf) = nullptr;
    pcg_span *c = nullptr;

    if (nr_cuts) {
        buf = mem_alloc(sizeof(*c), .nr = nr_cuts);
        if (!buf)   return cres_error(size_t, CERR_NOMEM);
        c = buf;
    }

    /* sort the cuts by their start, merge the ones that touch */
    size_t nc = 0;
    for (size_t i = 0; i < nr_cuts; i++) {
        size_t j = nc++;

        for (; j > 0 && cuts[i].lo < c[j - 1].lo; j--)  c[j] = c[j - 1];
        c[j] = cuts[i];
    }

    size_t nm = 0;
    for (size_t i = 0; i < nc; i++)
        if (nm && c[i].lo <= c[nm - 1].hi + merge)      c[nm - 1].hi = fmaxf(c[nm - 1].hi, c[i].hi);
        else                                            c[nm++] = c[i];

    /* the runs between the cuts */
    size_t nr = 0;
    float at = run->lo;
    for (size_t k = 0; k <= nm; k++) {
        float end = k < nm ? fminf(c[k].lo, run->hi) : run->hi;

        if (end - at > min_run) {
            if (nr == cap)  return cres_error(size_t, CERR_TOO_LARGE);
            out[nr++] = (pcg_span){ .lo = at, .hi = end };
        }
        if (k < nm) at = fmaxf(at, c[k].hi);
    }

    return cres_val(size_t, nr);
}

cres(size_t) pcg_segment_runs(const vec2 a, const vec2 b, const pcg_region *cuts, size_t nr,
                              const pcg_span_tol *tol, pcg_span *out, size_t cap)
{
    if (!tol || (nr && !cuts))  return cres_error(size_t, CERR_INVALID_ARGUMENTS);

    LOCAL_SET(void, buf) = nullptr;
    pcg_span *c = nullptr;

    if (nr) {
        buf = mem_alloc(sizeof(*c), .nr = nr);
        if (!buf)   return cres_error(size_t, CERR_NOMEM);
        c = buf;
    }

    size_t nc = 0;
    for (size_t i = 0; i < nr; i++)
        if (pcg_region_cut(&cuts[i], a, b, tol->min_frac, &c[nc]))  nc++;

    vec2 d;

    vec2_sub(d, (float *)b, (float *)a);
    pcg_span run = { .lo = 0.0f, .hi = vec2_len(d) };

    return pcg_span_subtract(&run, c, nc, tol->merge, tol->min_run, out, cap);
}
