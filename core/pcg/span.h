/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_SPAN_H__
#define __CLAP_PCG_SPAN_H__

#include "error.h"
#include "linmath.h"
#include "pcg/frame.h"

/*
 * Runs along a straight line and the cuts that interrupt them: a railing
 * that breaks where a stair lands, a band that breaks over a flight, a
 * wall around its doors. A run is a closed interval of distance along its
 * line; cuts come as intervals (pcg_opening) or as regions of the ground
 * plane (pcg_region) that a segment crosses.
 */

/**
 * struct pcg_span - a closed interval along a line
 * @lo: start
 * @hi: end
 */
typedef struct pcg_span {
    float   lo;
    float   hi;
} pcg_span;

/**
 * struct pcg_range - an interval whose ends may be unbounded
 * @lo:         lower bound, unless @open_lo
 * @hi:         upper bound, unless @open_hi
 * @open_lo:    no lower bound: the range reaches out without end below
 * @open_hi:    no upper bound: the range reaches out without end above
 */
typedef struct pcg_range {
    float   lo;
    float   hi;
    bool    open_lo;
    bool    open_hi;
} pcg_range;

/**
 * struct pcg_region - a window of the ground plane that cuts runs
 * @frame:  the window's frame, in the coordinates of the segments it
 *          cuts; only its (x, z) placement counts
 * @along:  the window's extent along @frame's +a
 * @across: the window's extent along @frame's +c
 *
 * The points of the plane inside both ranges. A flight's boarding window,
 * for one: from where the treads clear a walker's head to the far end of
 * its landing, and from the chord out to the flight's far edge.
 */
typedef struct pcg_region {
    pcg_frame   frame;
    pcg_range   along;
    pcg_range   across;
} pcg_region;

/**
 * struct pcg_opening - a doorway, portal or other hole in a run
 * @at: center along the run
 * @w:  width along the run
 * @h:  head height in the run's frame; a head outside the element's
 *      vertical extent leaves the opening full height, with nothing
 *      above it
 */
typedef struct pcg_opening {
    float   at;
    float   w;
    float   h;
} pcg_opening;

/**
 * pcg_opening_span() - the interval of a run an opening takes out
 * @o:  the opening
 *
 * Return: [@o->at - @o->w / 2, @o->at + @o->w / 2]
 */
static inline pcg_span pcg_opening_span(const pcg_opening *o)
{
    return (pcg_span){ .lo = o->at - o->w * 0.5f, .hi = o->at + o->w * 0.5f };
}

/**
 * pcg_openings_check() - check that openings fit a run
 * @open:   the openings, ordered along the run
 * @nr:     number of openings
 * @len:    the run's length; it spans [0, @len]
 *
 * Every opening needs a positive width and must lie within the run, and
 * each one must start no earlier than the previous one ends.
 *
 * Return: CERR_OK, or CERR_INVALID_ARGUMENTS with the offending opening's
 * index in the reason
 */
cerr_check pcg_openings_check(const pcg_opening *open, size_t nr, float len);

/**
 * pcg_regions_check() - check that regions have finite frames and bounds
 * @r:  the regions
 * @nr: number of regions
 *
 * Every region's frame must pass pcg_frame_check(), and every bounded end
 * of either range of every region must be finite: an end without a bound
 * is open, never an infinity.
 *
 * Return: CERR_OK, or CERR_INVALID_ARGUMENTS: what pcg_frame_check() turns
 * down, or a bound that isn't finite, with the offending region's index
 * in the reason
 */
cerr_check pcg_regions_check(const pcg_region *r, size_t nr);

/**
 * pcg_region_cut() - the part of a segment inside a region
 * @r:          the region
 * @a:          segment start, in the coordinates of @r's frame's parent
 * @b:          segment end
 * @min_frac:   cuts covering no more than this fraction of the segment
 *              don't count
 * @cut:        the cut, in distance from @a, written on success
 *
 * The segment's length is sqrtf() of its squared components. A bounded
 * end of either range of @r becomes a half-plane, and the segment is
 * clipped to them by poly2d_segment_clip(); unbounded ends add none.
 *
 * Return: true if the segment crosses @r over more than @min_frac of its
 * length
 */
bool pcg_region_cut(const pcg_region *r, const vec2 a, const vec2 b, float min_frac, pcg_span *cut);

/**
 * pcg_span_subtract() - the runs left of a span after cuts
 * @run:        the span to cut
 * @cuts:       the cuts, in any order; @lo <= @hi each
 * @nr_cuts:    number of cuts
 * @merge:      cuts no farther apart than this merge into one, and the
 *              piece between them goes
 * @min_run:    runs no longer than this are dropped
 * @out:        the runs, in order along @run
 * @cap:        capacity of @out; @nr_cuts + 1 runs always fit
 *
 * Cuts reaching past @run's ends are clipped to it.
 *
 * Return: the number of runs in @out; CERR_INVALID_ARGUMENTS for a
 * reversed span or cut, or a negative @merge or @min_run; CERR_TOO_LARGE
 * if the runs don't fit in @cap; CERR_NOMEM
 */
cres_check(size_t) pcg_span_subtract(const pcg_span *run, const pcg_span *cuts, size_t nr_cuts, float merge,
                                     float min_run, pcg_span *out, size_t cap);

/**
 * struct pcg_span_tol - how a cut segment's runs are tidied up
 * @min_frac:   region crossings no longer than this fraction of the
 *              segment are ignored (see pcg_region_cut())
 * @merge:      cuts no farther apart than this merge into one
 * @min_run:    runs no longer than this are dropped
 */
typedef struct pcg_span_tol {
    float   min_frac;
    float   merge;
    float   min_run;
} pcg_span_tol;

/**
 * pcg_segment_runs() - the runs of a segment outside a set of regions
 * @a:      segment start
 * @b:      segment end
 * @cuts:   regions that cut the segment
 * @nr:     number of regions
 * @tol:    tolerances
 * @out:    the runs, in distance from @a, in order
 * @cap:    capacity of @out; @nr + 1 runs always fit
 *
 * Cuts the segment with every region (pcg_region_cut()), then subtracts
 * the cuts from the segment's whole length (pcg_span_subtract()).
 *
 * Return: the number of runs in @out, or the pcg_span_subtract() errors
 */
cres_check(size_t) pcg_segment_runs(const vec2 a, const vec2 b, const pcg_region *cuts, size_t nr,
                                    const pcg_span_tol *tol, pcg_span *out, size_t cap);

#endif /* __CLAP_PCG_SPAN_H__ */
