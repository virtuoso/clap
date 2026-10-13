/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_ATRIUM_H__
#define __CLAP_PCG_ATRIUM_H__

#include "error.h"
#include "linmath.h"
#include "pcg/column.h"
#include "pcg/facade.h"
#include "pcg/flight.h"
#include "pcg/gallery.h"
#include "pcg/room-row.h"
#include "pcg/sink.h"
#include "pcg/skylight.h"
#include "pcg/slab.h"

/**
 * struct pcg_atrium_ring - one concentric ring of rooms on a floor
 * @rooms:      the row of rooms laid along every side of the ring; a
 *              depth given as a fraction is a fraction of the
 *              perimeter's circumradius
 * @nr_rooms:   number of @rooms
 * @inset:      the ring's back line, in from the facade's inner face: the
 *              ring's polygon is the perimeter's, its apothem shorter by
 *              the facade's thickness and @inset
 */
typedef struct pcg_atrium_ring {
    const pcg_room_entry    *rooms;
    size_t                  nr_rooms;
    float                   inset;
} pcg_atrium_ring;

/**
 * struct pcg_atrium_floor - the rooms of a floor
 * @rings:      the rings, outermost first
 * @nr_rings:   number of @rings
 */
typedef struct pcg_atrium_floor {
    const pcg_atrium_ring   *rings;
    size_t                  nr_rings;
} pcg_atrium_floor;

/**
 * struct pcg_atrium_params - an atrium office building's style
 * @floors:         storeys; galleries wrap the well on levels 1 to
 *                  @floors - 1, the roof is level @floors
 * @floor_h:        storey height, from one level's structure to the next
 * @sides:          the perimeter is a regular polygon of this many sides
 * @well_sides:     the well is a regular polygon of this many sides, in
 *                  the same orientation, centered in the perimeter
 * @entrance:       the ground floor side with the entrance portal in its
 *                  middle; negative for none
 * @radius:         the perimeter's circumradius
 * @entrance_w:     the entrance portal's width
 * @wall_overlap:   how far the facade's and the rooms' walls run up into
 *                  the slab over their storey
 * @well_r:         the well's circumradius per level: entry l - 1 sizes
 *                  the hole in level l (1 the first gallery, @floors the
 *                  roof and the skylight's aperture); levels past the
 *                  last entry take the last entry
 * @nr_well_r:      number of @well_r entries
 * @room_floors:    the rooms of every floor: floor f, 0 the ground floor,
 *                  takes entry f % @nr_room_floors; nullptr: no rooms
 * @nr_room_floors: number of @room_floors
 * @ground:         the ground slab and its finish, without a well
 * @gallery:        every gallery level; its fascia also hangs under the
 *                  roof's rim
 * @roof:           the roof slab
 * @column:         the columns at the well's corners, a ring per storey
 * @flight:         the flights spiralling up the well, rails and aprons
 *                  included
 * @facade:         the perimeter wall of every storey
 * @rooms:          every room's style, and their signs' text
 * @skylight:       the skylight over the roof's aperture
 *
 * A storeyed building around a central well under a skylight: the
 * ground slab, gallery levels around the well, the roof, a ring of
 * columns at the well's corners on every storey, a flight up every
 * storey but the top one, a facade around every storey and rings of
 * rooms between the facade and the well. Side 0 of both polygons faces
 * -c, and their points run counterclockwise in (a, c). Flight f climbs
 * from level f to f + 1 along the chord from point 2f of level f's well
 * (level 1's for the ground flight) to point 2f + 1 of level f + 1's,
 * so the flights spiral the well, an arrival and the next departure one
 * gallery edge apart; the galleries' rails and fascias break over the
 * flights' boarding windows. Every level is a batching chunk of its
 * own: a flight goes with the level it departs.
 */
typedef struct pcg_atrium_params {
    int                     floors;
    float                   floor_h;
    int                     sides;
    int                     well_sides;
    int                     entrance;
    float                   radius;
    float                   entrance_w;
    float                   wall_overlap;
    const float             *well_r;
    size_t                  nr_well_r;
    const pcg_atrium_floor  *room_floors;
    size_t                  nr_room_floors;
    pcg_slab_params         ground;
    pcg_gallery_params      gallery;
    pcg_slab_params         roof;
    pcg_column_params       column;
    pcg_flight_params       flight;
    pcg_facade_params       facade;
    pcg_room_row_params     rooms;
    pcg_skylight_params     skylight;
} pcg_atrium_params;

/**
 * pcg_atrium_params_init() - the reference atrium building
 * @p:  the params to fill, every field
 *
 * Four 6 m storeys in a hexagon of 60 m around a hexagonal well that
 * widens from 20 m at the first gallery to 24 m and then 28 m at the
 * roof, the entrance 2.6 m wide on side 1. The ground slab is 0.8 m
 * deep, the galleries' and the roof's 0.35 m, all but the roof under a
 * 0.05 m finish; walls run 0.05 m into the slab above. The columns are
 * 0.5 m square on 0.675 m bases 0.55 m tall, all cladding. The flights
 * are 5 m wide with 50 risers of 0.12 m and 0.20 m goings on a 0.22 m
 * waist, their aprons 0.28 m thick, wearing the galleries' rails. The
 * facade is the window wall of pcg_window_wall_params_init() with its
 * mullions in cladding. Every floor has a ring of two glass-fronted
 * offices per side, 0.3 of the side wide and 4.2 m deep with a doorway,
 * and 6.5 m further in a ring of three glazed front and back, 0.2 wide
 * and 3.6 m deep, their signs reading the floor and a two-digit room
 * number, "302". The rest is the children's defaults.
 */
void pcg_atrium_params_init(pcg_atrium_params *p);

/**
 * struct pcg_atrium_site - where an atrium goes
 * @frame:      the footprint's center on the top of the ground slab's
 *              structure, under its finish; level l's structure tops out
 *              at l * floor_h
 * @headroom:   the clear height a walker needs: a gallery's rail and its
 *              fascia stay clear of the flight arriving at it until a
 *              walker on its treads has this much over them. A navigation
 *              mesh's agent height, or the floors come apart in it.
 */
typedef struct pcg_atrium_site {
    pcg_frame   frame;
    float       headroom;
} pcg_atrium_site;

/**
 * _pcg_atrium_emit() - emit an atrium building
 * @ctx:    where it goes; level l batches in chunk @ctx->chunk + l
 * @p:      the style
 * @site:   the fit
 *
 * Derives every part's site from @p and checks every part's measures
 * and fit before any of them is emitted. What only shows when a part is
 * emitted leaves the parts emitted before it in the sink: a slab outline
 * the triangulation turns down, a surface the sink can't resolve against
 * @ctx's palette (a role the palette lacks, an explicit material missing
 * or of the wrong UV mode, signs without a SIGN material), or a room's
 * visit hook that fails. The room rings are laid out once for the checks
 * and again to emit them, so every room's entry fn and the label fn run
 * twice for it and must pick the same both times (see pcg_room_fn).
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx, @p or @site, for
 * a frame pcg_frame_check() turns down, fewer than 2 floors, a perimeter
 * or a well of fewer than 3 sides, a storey height, radius or well
 * radius that isn't positive and finite, a well that isn't inside the
 * perimeter at every level, an entrance past the last side or without a
 * positive and finite width, a wall overlap or headroom that is negative
 * or isn't finite, room floors or rings missing, a ring inset that is
 * negative or isn't finite or leaves no ring, or levels past the last
 * chunk; CERR_TOO_LARGE for more points or flights than memory holds;
 * the children's checks' errors, among them a storey too short for its
 * flight's landings; CERR_NOMEM; the children's emits' errors
 */
cerr_check _pcg_atrium_emit(const pcg_ctx *ctx, const pcg_atrium_params *p, const pcg_atrium_site *site);

/**
 * define pcg_atrium_emit - emit an atrium at a site described by pcg_atrium_site initializers
 *
 * Syntax sugar for _pcg_atrium_emit().
 */
#define pcg_atrium_emit(_ctx, _p, args...) _pcg_atrium_emit((_ctx), (_p), &(const pcg_atrium_site){ args })

#endif /* __CLAP_PCG_ATRIUM_H__ */
