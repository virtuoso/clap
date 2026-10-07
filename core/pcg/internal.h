/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_INTERNAL_H__
#define __CLAP_PCG_INTERNAL_H__

#include <math.h>
#include "poly2d.h"
#include <math.h>
#include "primitives.h"
#include "pcg/frame.h"

/*
 * Helpers shared by the pcg sources, all static inline: a debug build
 * keeps every function of an archive member it extracts, so nothing here
 * may drag a translation unit along.
 */

/* @lo <= @hi, false for a NaN on either side */
static inline bool pcg_ordered(float lo, float hi)
{
    return lo <= hi;
}

/* @x > 0, false for a NaN */
static inline bool pcg_positive(float x)
{
    return x > 0.0f;
}

/* @x >= 0, false for a NaN */
static inline bool pcg_nonnegative(float x)
{
    return x >= 0.0f;
}

/* @x > 0 and finite: false for a NaN and the infinities */
static inline bool pcg_positive_finite(float x)
{
    return pcg_positive(x) && isfinite(x);
}

/* @x >= 0 and finite: false for a NaN and the infinities */
static inline bool pcg_nonnegative_finite(float x)
{
    return pcg_nonnegative(x) && isfinite(x);
}

/*
 * _prim_emit_face(), with U and V swapped on every corner under @swap:
 * the transposed chart is the same face with c10 and c01 trading places
 * over the swapped rectangle, wound the other way to face the same side
 */
static inline void pcg_face(vec3 c00, vec3 c10, vec3 c11, vec3 c01, const vec4 uv_rect, bool swap,
                            const prim_emit_opts *opts)
{
    if (!swap) {
        _prim_emit_face(c00, c10, c11, c01, uv_rect, opts);
        return;
    }

    prim_emit_opts o = *opts;
    o.clockwise = !o.clockwise;
    _prim_emit_face(c00, c01, c11, c10, (vec4){ uv_rect[1], uv_rect[0], uv_rect[3], uv_rect[2] }, &o);
}

/* pcg_face() with the corners in @f's local (a, y, c) */
static inline void pcg_frame_face(const pcg_frame *f, const vec3 l00, const vec3 l10, const vec3 l11,
                                  const vec3 l01, const vec4 uv_rect, bool swap, const prim_emit_opts *opts)
{
    vec3 c00, c10, c11, c01;

    pcg_frame_point(f, l00, c00);
    pcg_frame_point(f, l10, c10);
    pcg_frame_point(f, l11, c11);
    pcg_frame_point(f, l01, c01);
    pcg_face(c00, c10, c11, c01, uv_rect, swap, opts);
}

/**
 * pcg_floats_finite() - check an array of floats
 * @v:  the floats
 * @nr: number of @v
 *
 * Return: true if all of @v are finite
 */
static inline bool pcg_floats_finite(const float *v, size_t nr)
{
    for (size_t i = 0; i < nr; i++)
        if (!isfinite(v[i]))    return false;

    return true;
}

/*
 * A path of at least 2 points, 3 if @closed, every point finite and every
 * edge, the closing one too if @closed, of some length; @what names the
 * path in the reasons
 */
static inline cerr pcg_path_check(const vec2 *pts, size_t nr, bool closed, const char *what)
{
    if (!pts || nr < (closed ? 3u : 2u))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "%s of too few points", .arg0 = what);

    for (size_t i = 0; i < nr; i++)
        if (!isfinite(pts[i][0]) || !isfinite(pts[i][1]))
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "%s point %zu isn't finite",
                .arg0   = what,
                .arg1   = (void *)(uintptr_t)i
            );

    size_t nr_edges = closed ? nr : nr - 1;
    for (size_t i = 0; i < nr_edges; i++) {
        vec2 dir, left;

        if (!(poly2d_edge_frame(pts[i], pts[(i + 1) % nr], dir, left) > 0.0f))
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "%s edge %zu has no length",
                .arg0   = what,
                .arg1   = (void *)(uintptr_t)i
            );
    }

    return CERR_OK;
}

#endif /* __CLAP_PCG_INTERNAL_H__ */
