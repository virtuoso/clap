/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_FLIGHT_H__
#define __CLAP_PCG_FLIGHT_H__

#include "error.h"
#include "linmath.h"
#include "pcg/balustrade.h"
#include "pcg/sink.h"
#include "pcg/span.h"
#include "pcg/stair.h"
#include "pcg/surface.h"

/**
 * struct pcg_flight_params - a stair flight's style, with its rails and aprons
 * @stair:          the flight itself
 * @rail:           its balustrades: sloped over the run, level along the
 *                  landings
 * @depart:         the departure apron's finish; a PCG_ROLE_FLOOR apron
 *                  wears the floor's own finish, which runs on across the
 *                  rim: plates chart the sink-space (x, z)
 * @arrive:         the arrival apron's finish
 * @gap:            clearance from the chord to the flight's near edge
 * @rail_inset:     every rail line runs this far toward +c of a flight
 *                  edge: the near rail on the treads, the far one past
 *                  the far edge
 * @rim_clear:      a landing rail stops this far inside the void from
 *                  where its line crosses the void's rim
 * @min_landing:    the shortest landing the chord may leave at either end
 *                  of the run
 * @min_apron:      aprons reaching less far than this along or across
 *                  are left out
 * @air:            clearance the boarding windows add to the heights they
 *                  keep clear, see pcg_flight_depart_cut()
 * @rail_near:      emit the near sloped rail
 * @rail_far:       emit the far sloped rail and the landing rails along
 *                  its line
 *
 * A straight flight climbing between two floors along the edge of a void
 * in both: the run centered on a chord between the floors' rims, its near
 * edge @gap off the chord, the landings at either end taking half of the
 * chord the run leaves each. Each landing is an apron: the part of its
 * floor's void up to the flight's far edge, behind the first riser on the
 * departure floor and on from the last tread on the arrival one, so
 * the aprons fill to the rims wherever they recede and yield to the floor
 * wherever the chord oversails it. Both are the stair's landing_t thick,
 * flush with their floors, and fan-triangulated. A level rail runs along
 * each landing on the far rail line, from the rim to the run, and stands
 * on its floor.
 */
typedef struct pcg_flight_params {
    pcg_stair_params        stair;
    pcg_balustrade_params   rail;
    pcg_surface             depart;
    pcg_surface             arrive;
    float                   gap;
    float                   rail_inset;
    float                   rim_clear;
    float                   min_landing;
    float                   min_apron;
    float                   air;
    bool                    rail_near;
    bool                    rail_far;
} pcg_flight_params;

/**
 * pcg_flight_params_init() - a generic flight's style
 * @p:  the params to fill, every field
 *
 * The stair and the rail of pcg_stair_params_init() and
 * pcg_balustrade_params_init(), 0.03 off the chord with both rails 0.03
 * in, landing rails 0.06 clear of the rims, landings of at least 0.6,
 * aprons of at least 0.05 and 0.1 of air. The departure apron is
 * PCG_ROLE_FLOOR, the arrival apron PCG_ROLE_STRUCTURE, both
 * PCG_COLLIDE_SOLID.
 */
void pcg_flight_params_init(pcg_flight_params *p);

/**
 * struct pcg_flight_site - one flight between two floor edges
 * @frame:          the chord's start on the departure floor: +a along the
 *                  chord, +c toward the flight, y the departure floor
 * @len:            chord length; the run is centered on it
 * @rise:           the climb to the arrival floor
 * @depart_void:    the void in the departure floor, convex, in @frame's
 *                  parent's (x, z); nullptr: no departure apron and no
 *                  landing rail there, where the floor itself runs under
 *                  the first riser
 * @nr_depart_void: number of points
 * @arrive_void:    the same in the arrival floor; nullptr: none there
 * @nr_arrive_void: number of points
 *
 * The stair and the rails chart @frame's local meters; the aprons chart
 * the sink-space (x, z) like every plate, and their side faces run V up
 * @frame's local y.
 */
typedef struct pcg_flight_site {
    pcg_frame   frame;
    float       len;
    float       rise;
    const vec2  *depart_void;
    size_t      nr_depart_void;
    const vec2  *arrive_void;
    size_t      nr_arrive_void;
} pcg_flight_site;

/**
 * pcg_flight_check() - check a flight's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * The checks every emit makes before it emits anything. A parent that
 * places a flight among other children makes them before any of its
 * children emits, so that a bad flight leaves nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site,
 * what pcg_stair_params_check() or pcg_balustrade_params_check() turn
 * down, a gap, rim clearance, minimum landing, minimum apron or air that
 * is negative or isn't finite, a rail inset that isn't finite, a frame
 * pcg_frame_check() turns down, a chord length that isn't positive and
 * finite, a run that leaves a landing shorter than @p->min_landing, a
 * void of fewer than 3 points, missing, with a point that isn't finite
 * or an edge without length, or that isn't convex: with a point on the
 * far side of an edge's line, or without an area; the pcg_stair_steps()
 * errors
 */
cerr_check pcg_flight_check(const pcg_flight_params *p, const pcg_flight_site *site);

/**
 * _pcg_flight_emit() - emit a flight with its rails and aprons
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Checks everything and works out the aprons before it emits anything.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx and for what
 * pcg_flight_check() turns down; CERR_NOMEM; the poly2d_clip(),
 * pcg_stair_emit(), pcg_balustrade_emit() and pcg_plate() errors
 */
cerr_check _pcg_flight_emit(const pcg_ctx *ctx, const pcg_flight_params *p, const pcg_flight_site *site);

/**
 * define pcg_flight_emit - emit a flight at a site described by pcg_flight_site initializers
 *
 * Syntax sugar for _pcg_flight_emit().
 */
#define pcg_flight_emit(_ctx, _p, args...) _pcg_flight_emit((_ctx), (_p), &(const pcg_flight_site){ args })

/**
 * pcg_flight_depart_cut() - where the departure floor must stay clear
 * @p:          the flight's style
 * @site:       its fit
 * @stand_h:    the height of what stands on the departure floor, a rail
 * @out:        the window, written on success
 *
 * The flight's soffit passes low over the departure floor beside the
 * run: whatever stands there is in its way until the soffit clears it.
 * The window reaches along the chord from behind the run up to where the
 * soffit, a waist and a riser below the treads, clears @stand_h by
 * @p->air:
 *
 *   land + run * (@stand_h + waist + rise / steps + air) / rise
 *
 * and across it from behind the chord out to the flight's far edge,
 * @p->gap + the stair's width. Everything else on the floor, a rail along
 * the rim for one, takes it as a cut.
 *
 * Return: CERR_OK, with @out in @site->frame's parent's coordinates; what
 * pcg_flight_check() turns down; CERR_INVALID_ARGUMENTS for a @stand_h
 * that is negative or isn't finite
 */
cerr_check pcg_flight_depart_cut(const pcg_flight_params *p, const pcg_flight_site *site, float stand_h,
                                 pcg_region *out);

/**
 * pcg_flight_arrive_cut() - where the arrival floor must stay clear
 * @p:          the flight's style
 * @site:       its fit
 * @headroom:   clear height a walker on the treads needs
 * @hang:       how far what is cut hangs below the arrival floor
 * @out:        the window, written on success
 *
 * The arrival floor's edge passes over the top of the run: anything at
 * it, or hanging @hang below it, cuts into the headroom of a walker on
 * the treads until the treads have climbed close enough. The window
 * reaches along the chord from where the treads are @headroom + @hang +
 * @p->air below the arrival floor:
 *
 *   len - land - (@headroom + @hang + air) / (rise / steps / going)
 *
 * on past the landing, and across it from behind the chord out to the
 * flight's far edge, @p->gap + the stair's width. A rail along the rim
 * takes it as a cut with no @hang, a fascia under the rim with its own
 * depth and the floor's finish above it: a navigation mesh severs the
 * floors if either leaves the walker less than its agent height.
 *
 * Return: CERR_OK, with @out in @site->frame's parent's coordinates; what
 * pcg_flight_check() turns down; CERR_INVALID_ARGUMENTS for a @headroom
 * or @hang that is negative or isn't finite
 */
cerr_check pcg_flight_arrive_cut(const pcg_flight_params *p, const pcg_flight_site *site, float headroom,
                                 float hang, pcg_region *out);

#endif /* __CLAP_PCG_FLIGHT_H__ */
