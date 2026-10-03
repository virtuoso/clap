// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include "poly2d.h"
#include "util.h"

/**
 * POLY2D_PARALLEL_EPS - spread below which a segment runs parallel to a line
 *
 * The spread of a segment's ends across a boundary line below which
 * poly2d_segment_clip() treats the segment as parallel to it: dividing by
 * the spread would put the crossing anywhere along the segment.
 */
#define POLY2D_PARALLEL_EPS 1e-6f

void poly2d_regular_vertex(vec2 out, int j, int n, float r, float phase)
{
    float th = phase + j * 2.0f * (float)M_PI / n;

    out[0] = r * cosf(th);
    out[1] = r * sinf(th);
}

void poly2d_regular(vec2 *out, int n, float r, float phase)
{
    for (int j = 0; j < n; j++)     poly2d_regular_vertex(out[j], j, n, r, phase);
}

float poly2d_phase_edge0_neg_z(int n)
{
    /* vertices 0 and 1 straddle -pi/2 by half a side each */
    return -(float)M_PI * 0.5f - (float)M_PI / n;
}

void poly2d_bbox(const vec2 *pts, size_t n, vec2 lo, vec2 hi)
{
    if (!n) {
        lo[0] = lo[1] = hi[0] = hi[1] = 0.0f;
        return;
    }

    lo[0] = hi[0] = pts[0][0];
    lo[1] = hi[1] = pts[0][1];
    for (size_t j = 1; j < n; j++) {
        lo[0] = fminf(lo[0], pts[j][0]);
        lo[1] = fminf(lo[1], pts[j][1]);
        hi[0] = fmaxf(hi[0], pts[j][0]);
        hi[1] = fmaxf(hi[1], pts[j][1]);
    }
}

cres(size_t) poly2d_clip(const vec2 *in, size_t n, const poly2d_halfplane *hp, vec2 *out, size_t cap)
{
    if (!hp || (n && (!in || !out)))    return cres_error(size_t, CERR_INVALID_ARGUMENTS);

    size_t nout = 0;

    for (size_t j = 0; j < n; j++) {
        const float *a = in[j], *b = in[(j + 1) % n];
        float da = a[0] * hp->n[0] + a[1] * hp->n[1] - hp->d;
        float db = b[0] * hp->n[0] + b[1] * hp->n[1] - hp->d;
        bool keep = da <= 0.0f, cross = (da <= 0.0f) != (db <= 0.0f);

        if (nout + keep + cross > cap)
            return cres_error(size_t, CERR_TOO_LARGE, .fmt = "clipping %zu points overflows %zu",
                              .arg0 = (void *)(uintptr_t)n, .arg1 = (void *)(uintptr_t)cap);

        if (keep) {
            out[nout][0] = a[0];
            out[nout][1] = a[1];
            nout++;
        }
        if (cross) {
            float t = da / (da - db);

            out[nout][0] = a[0] + (b[0] - a[0]) * t;
            out[nout][1] = a[1] + (b[1] - a[1]) * t;
            nout++;
        }
    }

    return cres_val(size_t, nout);
}

float poly2d_chord(const vec2 *pts, size_t n, const vec2 org, const vec2 dir, float off,
                   float *lo, float *hi)
{
    vec2 left = { -dir[1], dir[0] };
    float mn = 0.0f, mx = 0.0f;
    bool any = false;

    for (size_t j = 0; j < n; j++) {
        const float *a = pts[j], *b = pts[(j + 1) % n];
        float ca = (a[0] - org[0]) * left[0] + (a[1] - org[1]) * left[1];
        float cb = (b[0] - org[0]) * left[0] + (b[1] - org[1]) * left[1];

        if ((ca <= off) == (cb <= off)) continue;

        float t = (off - ca) / (cb - ca);
        float al = (a[0] - org[0]) * dir[0] + (a[1] - org[1]) * dir[1];
        float bl = (b[0] - org[0]) * dir[0] + (b[1] - org[1]) * dir[1];
        float x = al + (bl - al) * t;

        if (!any)           { mn = mx = x; any = true; }
        else if (x < mn)    mn = x;
        else if (x > mx)    mx = x;
    }

    if (lo) *lo = mn;
    if (hi) *hi = mx;

    return mx - mn;
}

bool poly2d_segment_clip(const vec2 a, const vec2 b, const poly2d_halfplane *hp, size_t nr,
                         float *t0, float *t1)
{
    float lo = 0.0f, hi = 1.0f;

    for (size_t k = 0; k < nr; k++) {
        float va = a[0] * hp[k].n[0] + a[1] * hp[k].n[1] - hp[k].d;
        float vb = b[0] * hp[k].n[0] + b[1] * hp[k].n[1] - hp[k].d;

        if (fabsf(vb - va) < POLY2D_PARALLEL_EPS) {
            if (va > 0.0f)  return false;
            continue;
        }

        float tc = va / (va - vb);

        if (vb > va)    hi = fminf(hi, tc);
        else            lo = fmaxf(lo, tc);
    }

    if (hi <= lo)   return false;

    *t0 = lo;
    *t1 = hi;

    return true;
}

float poly2d_edge_frame(const vec2 a, const vec2 b, vec2 dir, vec2 left)
{
    dir[0] = b[0] - a[0];
    dir[1] = b[1] - a[1];

    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1]);

    if (len > 0.0f) {
        dir[0] /= len;
        dir[1] /= len;
    } else {
        dir[0] = dir[1] = 0.0f;
    }
    left[0] = -dir[1];
    left[1] = dir[0];

    return len;
}
