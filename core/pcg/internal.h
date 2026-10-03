/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_INTERNAL_H__
#define __CLAP_PCG_INTERNAL_H__

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

#endif /* __CLAP_PCG_INTERNAL_H__ */
