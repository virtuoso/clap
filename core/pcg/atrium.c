// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "memory.h"
#include "poly2d.h"
#include "pcg/atrium.h"
#include "pcg/internal.h"

/* the reference building's outer ring: glass-fronted offices against the facade */
static const pcg_room_entry atrium_rooms_outer[] = {
    {
        .width = 0.3f, .width_frac = true, .depth = 4.2f,
        .wall_mask = PCG_ROOM_SOLID(PCG_ROOM_LEFT) | PCG_ROOM_SOLID(PCG_ROOM_RIGHT) |
                     PCG_ROOM_GLASS(PCG_ROOM_FRONT) | PCG_ROOM_DOOR(PCG_ROOM_FRONT),
    },
    {
        .width = 0.3f, .width_frac = true, .depth = 4.2f,
        .wall_mask = PCG_ROOM_SOLID(PCG_ROOM_LEFT) | PCG_ROOM_SOLID(PCG_ROOM_RIGHT) |
                     PCG_ROOM_GLASS(PCG_ROOM_FRONT) | PCG_ROOM_DOOR(PCG_ROOM_FRONT),
    },
};

/* the inner ring: glazed both ways, so the windows read from the well */
static const pcg_room_entry atrium_rooms_inner[] = {
    {
        .width = 0.2f, .width_frac = true, .depth = 3.6f,
        .wall_mask = PCG_ROOM_SOLID(PCG_ROOM_LEFT) | PCG_ROOM_SOLID(PCG_ROOM_RIGHT) |
                     PCG_ROOM_GLASS(PCG_ROOM_FRONT) | PCG_ROOM_GLASS(PCG_ROOM_BACK),
    },
    {
        .width = 0.2f, .width_frac = true, .depth = 3.6f,
        .wall_mask = PCG_ROOM_SOLID(PCG_ROOM_LEFT) | PCG_ROOM_SOLID(PCG_ROOM_RIGHT) |
                     PCG_ROOM_GLASS(PCG_ROOM_FRONT) | PCG_ROOM_GLASS(PCG_ROOM_BACK),
    },
    {
        .width = 0.2f, .width_frac = true, .depth = 3.6f,
        .wall_mask = PCG_ROOM_SOLID(PCG_ROOM_LEFT) | PCG_ROOM_SOLID(PCG_ROOM_RIGHT) |
                     PCG_ROOM_GLASS(PCG_ROOM_FRONT) | PCG_ROOM_GLASS(PCG_ROOM_BACK),
    },
};

static const pcg_atrium_ring atrium_rings_ref[] = {
    { .rooms = atrium_rooms_outer, .nr_rooms = array_size(atrium_rooms_outer), .inset = 0.0f },
    { .rooms = atrium_rooms_inner, .nr_rooms = array_size(atrium_rooms_inner), .inset = 6.5f },
};

static const pcg_atrium_floor atrium_floors_ref[] = {
    { .rings = atrium_rings_ref, .nr_rings = array_size(atrium_rings_ref) },
};

/* the reference well widens toward the skylight */
static const float atrium_well_r_ref[] = { 20.0f, 24.0f, 28.0f };

/* the reference sign text: the floor and a two-digit room number, "302" */
static void atrium_room_number(const pcg_room_tag *tag, void *priv, char *buf, size_t size)
{
    snprintf(buf, size, "%d%02d", tag->floor, tag->number);
}

void pcg_atrium_params_init(pcg_atrium_params *p)
{
    *p = (pcg_atrium_params){
        .floors         = 4,
        .floor_h        = 6.0f,
        .sides          = 6,
        .well_sides     = 6,
        .entrance       = 1,
        .radius         = 60.0f,
        .entrance_w     = 2.6f,
        .wall_overlap   = 0.05f,
        .well_r         = atrium_well_r_ref,
        .nr_well_r      = array_size(atrium_well_r_ref),
        .room_floors    = atrium_floors_ref,
        .nr_room_floors = array_size(atrium_floors_ref),
    };

    pcg_slab_params_init(&p->ground);
    p->ground.t = 0.8f;

    pcg_gallery_params_init(&p->gallery);

    pcg_slab_params_init(&p->roof);
    p->roof.t = 0.35f;
    p->roof.finish_t = 0.0f;

    pcg_column_params_init(&p->column);
    p->column.w = 0.5f;
    p->column.base_w = 0.675f;
    p->column.base_h = 0.55f;
    p->column.base.role = PCG_ROLE_CLADDING;

    pcg_flight_params_init(&p->flight);
    p->flight.stair.width = 5.0f;
    p->flight.stair.riser_max = 0.12f;
    p->flight.stair.going = 0.20f;
    p->flight.stair.waist = 0.22f;
    p->flight.stair.landing_t = 0.28f;
    p->flight.rail = p->gallery.rail;

    pcg_facade_params_init(&p->facade);
    p->facade.wall.mullion.role = PCG_ROLE_CLADDING;

    pcg_room_row_params_init(&p->rooms);
    p->rooms.label = atrium_room_number;

    pcg_skylight_params_init(&p->skylight);
}

/* the well's circumradius at @level; levels past the table take its last entry */
static float atrium_well_r(const pcg_atrium_params *p, int level)
{
    size_t i = level > 1 ? (size_t)(level - 1) : 0;

    return p->well_r[min(i, p->nr_well_r - 1)];
}

/* the finish a storey walks on: the ground's or a gallery's */
static float atrium_finish_t(const pcg_atrium_params *p, int storey)
{
    return storey ? p->gallery.floor.finish_t : p->ground.finish_t;
}

/* the slab over a storey: the next gallery's, or the roof */
static float atrium_slab_over(const pcg_atrium_params *p, int storey)
{
    return storey + 1 < p->floors ? p->gallery.floor.t : p->roof.t;
}

/* a level's frame in the building's: its structure's top */
static pcg_frame atrium_level_frame(const pcg_atrium_params *p, int level)
{
    return (pcg_frame){ .org = { 0.0f, level * p->floor_h, 0.0f }, .dir = { 1.0f, 0.0f } };
}

/* a storey's floor frame in the building's: the top of its finish */
static pcg_frame atrium_storey_frame(const pcg_atrium_params *p, int storey)
{
    return (pcg_frame){ .org = { 0.0f, storey * p->floor_h + atrium_finish_t(p, storey), 0.0f },
                        .dir = { 1.0f, 0.0f } };
}

/* the clear height of a storey, from its finish to the slab over it */
static float atrium_ceiling(const pcg_atrium_params *p, int storey)
{
    return p->floor_h - atrium_slab_over(p, storey) - atrium_finish_t(p, storey);
}

/* the height of a storey's walls, up into the slab over it */
static float atrium_wall_h(const pcg_atrium_params *p, int storey)
{
    return atrium_ceiling(p, storey) + p->wall_overlap;
}

static cerr atrium_params_check(const pcg_ctx *ctx, const pcg_atrium_params *p, const pcg_atrium_site *site)
{
    CERR_RET_CERR(pcg_frame_check(&site->frame));
    if (p->floors < 2)  return CERR_INVALID_ARGUMENTS_REASON(.fmt = "atrium of fewer than 2 floors");
    if (p->sides < 3 || p->well_sides < 3)
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "atrium perimeter or well of fewer than 3 sides");

    const float dims[] = { p->floor_h, p->radius, p->floors * p->floor_h };
    for (size_t i = 0; i < array_size(dims); i++)
        if (!pcg_positive_finite(dims[i]))
            return CERR_INVALID_ARGUMENTS_REASON(.fmt = "atrium floor_h, radius or height isn't positive and finite");

    if (!p->well_r || !p->nr_well_r)    return CERR_INVALID_ARGUMENTS_REASON(.fmt = "atrium well_r missing");
    for (size_t i = 0; i < p->nr_well_r; i++)
        if (!pcg_positive_finite(p->well_r[i]))
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "atrium well_r %zu isn't positive and finite",
                .arg0   = (void *)(uintptr_t)i
            );

    if (p->entrance >= p->sides)    return CERR_INVALID_ARGUMENTS_REASON(.fmt = "atrium entrance past the last side");
    if (p->entrance >= 0 && (!pcg_positive_finite(p->entrance_w)))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "atrium entrance_w isn't positive and finite");
    if (!pcg_nonnegative_finite(p->wall_overlap))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "atrium wall_overlap is negative or isn't finite");
    if (!pcg_nonnegative_finite(site->headroom))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "atrium headroom is negative or isn't finite");

    /* every level gets a chunk of its own */
    if (ctx->chunk > UINT_MAX - (unsigned int)p->floors)
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "atrium levels past the last chunk");

    if (p->nr_room_floors && !p->room_floors)
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "atrium room_floors missing");
    for (size_t f = 0; f < p->nr_room_floors; f++) {
        const pcg_atrium_floor *fl = &p->room_floors[f];

        if (fl->nr_rings && !fl->rings)
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "atrium room floor %zu rings missing",
                .arg0   = (void *)(uintptr_t)f
            );
        for (size_t r = 0; r < fl->nr_rings; r++)
            if (!pcg_nonnegative_finite(fl->rings[r].inset))
                return CERR_INVALID_ARGUMENTS_REASON(
                    .fmt    = "atrium room floor %zu ring %zu inset is negative or isn't finite",
                    .arg0   = (void *)(uintptr_t)f,
                    .arg1   = (void *)(uintptr_t)r
                );
    }

    return CERR_OK;
}

/*
 * struct atrium_plan - what every part of a building is derived from
 * @outline:    the perimeter, @sides points
 * @facade:     the facade's centerline, @sides points
 * @ring:       scratch for a room ring's polygon, @sides points
 * @feet:       scratch for a ring of columns, @well_sides points
 * @rims:       the well at every level, @well_sides points each; level l
 *              at @rims + l * well_sides, from level 1 on
 * @roof:       the roof slab's outline followed by its rim
 * @flight:     every flight's site, @floors - 1 of them
 * @cuts:       every flight's windows: where the departure floor's rail,
 *              the arrival floor's rail and the arrival floor's fascia
 *              break, ATRIUM_CUTS per flight
 */
typedef struct atrium_plan {
    vec2            *outline;
    vec2            *facade;
    vec2            *ring;
    vec2            *feet;
    vec2            *rims;
    vec2            *roof;
    pcg_flight_site *flight;
    pcg_region      *cuts;
} atrium_plan;

/**
 * enum atrium_cut - a flight's windows in &struct atrium_plan cuts
 * @ATRIUM_CUT_DEPART:  where the departure floor's rail breaks
 * @ATRIUM_CUT_ARRIVE:  where the arrival floor's rail breaks
 * @ATRIUM_CUT_FASCIA:  where the arrival floor's fascia breaks
 * @ATRIUM_CUTS:        windows per flight
 */
enum atrium_cut { ATRIUM_CUT_DEPART, ATRIUM_CUT_ARRIVE, ATRIUM_CUT_FASCIA, ATRIUM_CUTS };

static const vec2 *atrium_rim(const pcg_atrium_params *p, const atrium_plan *plan, int level)
{
    return plan->rims + (size_t)level * p->well_sides;
}

/* @pt strictly inside the convex polygon @poly, wound counterclockwise */
static bool atrium_inside(const vec2 *poly, int n, const vec2 pt)
{
    for (int i = 0; i < n; i++) {
        vec2 dir, left;

        poly2d_edge_frame(poly[i], poly[(i + 1) % n], dir, left);
        if (!((pt[0] - poly[i][0]) * left[0] + (pt[1] - poly[i][1]) * left[1] > 0.0f))  return false;
    }

    return true;
}

/* the flights and their windows; checks every flight's fit on the way */
static cerr atrium_plan_flights(const pcg_atrium_params *p, const pcg_atrium_site *site, atrium_plan *plan)
{
    int ws = p->well_sides;

    for (int f = 0; f < p->floors - 1; f++) {
        const vec2 *from = atrium_rim(p, plan, f ? f : 1), *to = atrium_rim(p, plan, f + 1);
        pcg_flight_site *fs = &plan->flight[f];
        pcg_region *cut = &plan->cuts[(size_t)f * ATRIUM_CUTS];

        *fs = (pcg_flight_site){
            .rise           = p->floor_h + (atrium_finish_t(p, f + 1) - atrium_finish_t(p, f)),
            .depart_void    = f ? from : nullptr,
            .nr_depart_void = f ? (size_t)ws : 0,
            .arrive_void    = to,
            .nr_arrive_void = (size_t)ws,
        };
        fs->len = CRES_RET_CERR(pcg_frame_run(&fs->frame, from[(2 * f) % ws], to[(2 * f + 1) % ws],
                                              atrium_storey_frame(p, f).org[1]));

        CERR_RET_CERR(pcg_flight_check(&p->flight, fs));
        CERR_RET_CERR(pcg_flight_depart_cut(&p->flight, fs, p->gallery.rail.h, &cut[ATRIUM_CUT_DEPART]));
        CERR_RET_CERR(pcg_flight_arrive_cut(&p->flight, fs, site->headroom, 0.0f, &cut[ATRIUM_CUT_ARRIVE]));
        CERR_RET_CERR(pcg_flight_arrive_cut(&p->flight, fs, site->headroom,
                                            p->gallery.fascia.depth + p->gallery.floor.finish_t,
                                            &cut[ATRIUM_CUT_FASCIA]));
    }

    return CERR_OK;
}

/* the polygons, the flights and their windows */
static cerr atrium_plan_get(const pcg_atrium_params *p, const pcg_atrium_site *site, atrium_plan *plan,
                            void **pts_buf, void **flight_buf, void **cut_buf)
{
    size_t n = (size_t)p->sides, ws = (size_t)p->well_sides, nr_flights = (size_t)p->floors - 1;
    size_t nr_levels = (size_t)p->floors + 1;

    /* the counts come from ints, so 64 bits hold them, a 32-bit size_t maybe not */
    uint64_t nr_pts = 4 * (uint64_t)n + 2 * (uint64_t)ws + (uint64_t)nr_levels * ws;
    uint64_t nr_cuts = (uint64_t)nr_flights * ATRIUM_CUTS;
    if (nr_pts > SIZE_MAX / sizeof(vec2) || nr_cuts > SIZE_MAX / sizeof(pcg_region))
        return CERR_TOO_LARGE_REASON(.fmt = "atrium of more points or flights than memory holds");

    *pts_buf = mem_alloc(sizeof(vec2), .nr = (size_t)nr_pts);
    *flight_buf = mem_alloc(sizeof(pcg_flight_site), .nr = nr_flights);
    *cut_buf = mem_alloc(sizeof(pcg_region), .nr = (size_t)nr_cuts);
    if (!*pts_buf || !*flight_buf || !*cut_buf)     return CERR_NOMEM;

    vec2 *pts = *pts_buf;
    *plan = (atrium_plan){
        .outline    = pts,
        .facade     = pts + n,
        .ring       = pts + 2 * n,
        .feet       = pts + 3 * n,
        .rims       = pts + 3 * n + ws,
        .roof       = pts + 3 * n + ws + nr_levels * ws,
        .flight     = *flight_buf,
        .cuts       = *cut_buf,
    };

    /* the facade straddles its centerline, a wall's half thickness in from the perimeter */
    int sides = p->sides;
    float phase = poly2d_phase_edge0_neg_z(sides);
    float apothem = p->radius * cosf((float)M_PI / sides);
    float rin = p->radius * (apothem - p->facade.wall.t * 0.5f) / apothem;

    poly2d_regular(plan->outline, sides, p->radius, phase);
    poly2d_regular(plan->facade, sides, rin, phase);

    /* every level's well, inside the perimeter */
    float wphase = poly2d_phase_edge0_neg_z(p->well_sides);
    for (int l = 1; l <= p->floors; l++) {
        vec2 *rim = plan->rims + (size_t)l * ws;

        poly2d_regular(rim, p->well_sides, atrium_well_r(p, l), wphase);
        for (size_t j = 0; j < ws; j++) {
            vec2 dir, left;

            if (!atrium_inside(plan->outline, sides, rim[j]))
                return CERR_INVALID_ARGUMENTS_REASON(
                    .fmt    = "atrium well at level %zu isn't inside the perimeter",
                    .arg0   = (void *)(uintptr_t)l
                );
            if (!(poly2d_edge_frame(rim[j], rim[(j + 1) % ws], dir, left) > 0.0f))
                return CERR_INVALID_ARGUMENTS_REASON(
                    .fmt    = "atrium well at level %zu has coincident points",
                    .arg0   = (void *)(uintptr_t)l
                );
        }
    }

    /* the roof: the perimeter around the top level's well */
    memcpy(plan->roof, plan->outline, n * sizeof(vec2));
    memcpy(plan->roof + n, atrium_rim(p, plan, p->floors), ws * sizeof(vec2));

    return atrium_plan_flights(p, site, plan);
}

/* a level's context: its own chunk */
static pcg_ctx atrium_chunk(const pcg_ctx *at, int level)
{
    pcg_ctx c = *at;

    c.chunk += (unsigned int)level;

    return c;
}

/* level @l's gallery: the slab around the well, the fascia and the rail, broken over the flights */
static cerr atrium_gallery(const pcg_ctx *at, const pcg_atrium_params *p, const atrium_plan *plan, int l,
                           bool emit)
{
    const pcg_region *arrive = &plan->cuts[(size_t)(l - 1) * ATRIUM_CUTS];
    pcg_region rail_cuts[2] = { arrive[ATRIUM_CUT_ARRIVE] };
    size_t nr_rail_cuts = 1;

    /* the flight departing this level, unless it's the top gallery */
    if (l < p->floors - 1)  rail_cuts[nr_rail_cuts++] = plan->cuts[(size_t)l * ATRIUM_CUTS + ATRIUM_CUT_DEPART];

    const pcg_gallery_site site = {
        .frame          = atrium_level_frame(p, l),
        .outline        = plan->outline,
        .nr_outline     = (size_t)p->sides,
        .rim            = atrium_rim(p, plan, l),
        .nr_rim         = (size_t)p->well_sides,
        .rail_cuts      = rail_cuts,
        .nr_rail_cuts   = nr_rail_cuts,
        .fascia_cuts    = &arrive[ATRIUM_CUT_FASCIA],
        .nr_fascia_cuts = 1,
    };

    if (!emit)  return pcg_gallery_check(&p->gallery, &site);

    pcg_ctx lc = atrium_chunk(at, l);

    return _pcg_gallery_emit(&lc, &p->gallery, &site);
}

/* the roof: its slab around the well, the fascia under its rim and the skylight over the well */
static cerr atrium_roof(const pcg_ctx *at, const pcg_atrium_params *p, const atrium_plan *plan, bool emit)
{
    int l = p->floors;
    size_t n = (size_t)p->sides, ws = (size_t)p->well_sides;
    const poly_ring rings[2] = { { .start = 0, .nr = n }, { .start = n, .nr = ws } };
    const pcg_slab_site slab = {
        .frame      = atrium_level_frame(p, l),
        .pts        = plan->roof,
        .nr_pts     = n + ws,
        .rings      = rings,
        .nr_rings   = 2,
    };
    const pcg_fascia_site fascia = {
        .frame      = slab.frame,
        .pts        = atrium_rim(p, plan, l),
        .nr_pts     = ws,
        .closed     = true,
    };
    const pcg_skylight_site skylight = { .frame = slab.frame, .sides = p->well_sides, .r = atrium_well_r(p, l) };

    if (!emit) {
        CERR_RET_CERR(pcg_slab_check(&p->roof, &slab));
        CERR_RET_CERR(pcg_fascia_check(&p->gallery.fascia, &fascia));

        return pcg_skylight_check(&p->skylight, &skylight);
    }

    pcg_ctx lc = atrium_chunk(at, l);

    CERR_RET_CERR(_pcg_slab_emit(&lc, &p->roof, &slab));
    CERR_RET_CERR(_pcg_fascia_emit(&lc, &p->gallery.fascia, &fascia));

    return _pcg_skylight_emit(&lc, &p->skylight, &skylight);
}

/*
 * Storey @s's columns: a ring at the corners of the wider of its two
 * wells, the one in its floor and the one in the slab it carries, so a
 * uniform well stacks them into continuous verticals and a widening one
 * steps them with its rims; the ground storey takes the first gallery's
 */
static cerr atrium_columns(const pcg_ctx *at, const pcg_atrium_params *p, const atrium_plan *plan, int s,
                           bool emit)
{
    float rc = s ? fmaxf(atrium_well_r(p, s), atrium_well_r(p, s + 1)) : atrium_well_r(p, 1);

    poly2d_regular(plan->feet, p->well_sides, rc, poly2d_phase_edge0_neg_z(p->well_sides));

    const pcg_columns_site site = {
        .frame  = atrium_storey_frame(p, s),
        .at     = plan->feet,
        .nr_at  = (size_t)p->well_sides,
        .h      = p->floor_h - atrium_finish_t(p, s),
    };

    if (!emit)  return pcg_columns_check(&p->column, &site);

    pcg_ctx lc = atrium_chunk(at, s);

    return _pcg_columns_emit(&lc, &p->column, &site);
}

/* storey @s's facade, with the entrance on the ground floor */
static cerr atrium_facade(const pcg_ctx *at, const pcg_atrium_params *p, const atrium_plan *plan, int s, bool emit)
{
    pcg_opening entrance = { .w = p->entrance_w, .h = p->facade.wall.head };
    pcg_facade_edge edge = { .openings = &entrance, .nr_openings = 1 };
    bool portal = !s && p->entrance >= 0;

    /* the portal in the middle of its side, as the facade measures it */
    if (portal) {
        pcg_frame run;
        int e = p->entrance;
        float len = CRES_RET_CERR(pcg_frame_run(&run, plan->facade[e], plan->facade[(e + 1) % p->sides], 0.0f));

        entrance.at = len * 0.5f;
        edge.edge = (size_t)e;
    }

    const pcg_facade_site site = {
        .frame      = atrium_storey_frame(p, s),
        .pts        = plan->facade,
        .nr_pts     = (size_t)p->sides,
        .h          = atrium_wall_h(p, s),
        .edges      = portal ? &edge : nullptr,
        .nr_edges   = portal ? 1 : 0,
    };

    if (!emit)  return pcg_facade_check(&p->facade, &site);

    pcg_ctx lc = atrium_chunk(at, s);

    return _pcg_facade_emit(&lc, &p->facade, &site);
}

/*
 * Storey @s's rooms: its floor's rings, each a row along every side of
 * the perimeter's polygon moved in to the ring's back line, numbered on
 * from the ring before; the ground floor leaves the entrance's side
 * clear on every ring, so the lobby stays open to the well
 */
static cerr atrium_rooms(const pcg_ctx *at, const pcg_atrium_params *p, const atrium_plan *plan, int s, bool emit)
{
    if (!p->nr_room_floors)     return CERR_OK;

    const pcg_atrium_floor *fl = &p->room_floors[(size_t)s % p->nr_room_floors];
    float apothem = p->radius * cosf((float)M_PI / p->sides);
    size_t lobby = (size_t)p->entrance;
    bool skip = !s && p->entrance >= 0;
    int number = 1;
    pcg_ctx lc = emit ? atrium_chunk(at, s) : (pcg_ctx){};

    for (size_t r = 0; r < fl->nr_rings; r++) {
        const pcg_atrium_ring *ring = &fl->rings[r];
        float rring = p->radius * (apothem - p->facade.wall.t - ring->inset) / apothem;

        if (!pcg_positive_finite(rring))
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "atrium room ring %zu has no room inside the facade",
                .arg0   = (void *)(uintptr_t)r
            );

        poly2d_regular(plan->ring, p->sides, rring, poly2d_phase_edge0_neg_z(p->sides));

        const pcg_room_ring_site site = {
            .frame      = atrium_storey_frame(p, s),
            .pts        = plan->ring,
            .nr_pts     = (size_t)p->sides,
            .depth_ref  = p->radius,
            .h          = atrium_wall_h(p, s),
            .ceiling    = atrium_ceiling(p, s),
            .rooms      = ring->rooms,
            .nr_rooms   = ring->nr_rooms,
            .skip       = skip ? &lobby : nullptr,
            .nr_skip    = skip ? 1 : 0,
            .tag        = { .floor = s, .ring = (int)r, .number = number },
        };

        if (emit)   CERR_RET_CERR(_pcg_room_ring_emit(&lc, &p->rooms, &site));
        else        CERR_RET_CERR(pcg_room_ring_check(&p->rooms, &site));

        number += (int)(ring->nr_rooms * (size_t)(p->sides - (skip ? 1 : 0)));
    }

    return CERR_OK;
}

/* every part of the building, level by level: emitted into @at with @emit, otherwise only checked */
static cerr atrium_parts(const pcg_ctx *at, const pcg_atrium_params *p, const atrium_plan *plan, bool emit)
{
    const pcg_slab_site ground = {
        .frame  = atrium_level_frame(p, 0),
        .pts    = plan->outline,
        .nr_pts = (size_t)p->sides,
    };

    if (emit) {
        pcg_ctx lc = atrium_chunk(at, 0);

        CERR_RET_CERR(_pcg_slab_emit(&lc, &p->ground, &ground));
    } else {
        CERR_RET_CERR(pcg_slab_check(&p->ground, &ground));
    }

    for (int s = 0; s < p->floors; s++) {
        if (s)  CERR_RET_CERR(atrium_gallery(at, p, plan, s, emit));
        CERR_RET_CERR(atrium_columns(at, p, plan, s, emit));

        /* a flight belongs to the level it departs; its site was checked with its windows */
        if (emit && s < p->floors - 1) {
            pcg_ctx lc = atrium_chunk(at, s);

            CERR_RET_CERR(_pcg_flight_emit(&lc, &p->flight, &plan->flight[s]));
        }

        CERR_RET_CERR(atrium_facade(at, p, plan, s, emit));
        CERR_RET_CERR(atrium_rooms(at, p, plan, s, emit));
    }

    return atrium_roof(at, p, plan, emit);
}

cerr _pcg_atrium_emit(const pcg_ctx *ctx, const pcg_atrium_params *p, const pcg_atrium_site *site)
{
    if (!ctx || !p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(atrium_params_check(ctx, p, site));

    LOCAL(void, pts_buf);
    LOCAL(void, flight_buf);
    LOCAL(void, cut_buf);
    atrium_plan plan;
    CERR_RET_CERR(atrium_plan_get(p, site, &plan, &pts_buf, &flight_buf, &cut_buf));

    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);

    CERR_RET_CERR(atrium_parts(&at, p, &plan, false));

    return atrium_parts(&at, p, &plan, true);
}
