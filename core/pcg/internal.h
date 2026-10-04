/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_INTERNAL_H__
#define __CLAP_PCG_INTERNAL_H__

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

#endif /* __CLAP_PCG_INTERNAL_H__ */
