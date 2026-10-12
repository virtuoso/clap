// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include "pcg/internal.h"
#include "pcg/room.h"
#include "pcg/shape.h"

void pcg_room_params_init(pcg_room_params *p)
{
    *p = (pcg_room_params){
        .door_w     = 1.1f,
        .door_h     = 2.2f,
        .carpet_t   = 0.012f,
        .carpet     = { .role = PCG_ROLE_CARPET, .collide = PCG_COLLIDE_NONE },
    };
    pcg_wall_params_init(&p->wall);
    pcg_glazing_params_init(&p->glass);
    pcg_fixture_params_init(&p->light);
    pcg_sign_params_init(&p->sign);
}

/* the room's own style and site; its children check their own */
static cerr room_site_check(const pcg_room_params *p, const pcg_room_site *site)
{
    CERR_RET_CERR(pcg_frame_check(&site->frame));

    const float dims[] = { p->door_w, p->door_h, p->carpet_t, site->w, site->d, site->h, site->ceiling };
    for (size_t i = 0; i < array_size(dims); i++)
        if (!pcg_positive_finite(dims[i]))
            return CERR_INVALID_ARGUMENTS_REASON(.fmt = "room dimension isn't positive and finite");

    if (!isfinite(site->a0))    return CERR_INVALID_ARGUMENTS_REASON(.fmt = "room start isn't finite");
    if (!pcg_positive_finite(p->wall.t))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "room wall t isn't positive and finite");

    /* an interior inside the walls on every side */
    float rt2 = 2.0f * p->wall.t;
    if (!(site->w > rt2) || !(site->d > rt2))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "room no wider or deeper than two walls");
    if (site->wall_mask & ~PCG_ROOM_MASK_ALL)
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "room wall mask has unknown bits");

    return CERR_OK;
}

/*
 * @local, given in the site frame's coordinates, in those of the site
 * frame's parent: the children go into the caller's context at frames
 * composed this way
 */
static pcg_frame room_frame(const pcg_room_site *site, const pcg_frame *local)
{
    pcg_frame f;

    pcg_frame_compose(&site->frame, local, &f);

    return f;
}

/*
 * The wall on @side: a solid wall, or the glass centered where it would
 * stand; emitted into @ctx with @emit, otherwise checked
 */
static cerr room_wall(const pcg_ctx *ctx, const pcg_room_params *p, const pcg_room_site *site, int side, bool emit)
{
    uint32_t mask = site->wall_mask;
    bool solid = mask & PCG_ROOM_SOLID(side);
    bool glass = !solid && mask & PCG_ROOM_GLASS(side);

    if (!solid && !glass)                               return CERR_OK;
    if (side == PCG_ROOM_LEFT && site->party_lo)        return CERR_OK;
    if (side == PCG_ROOM_RIGHT && site->party_hi)       return CERR_OK;

    float rt = p->wall.t, a0 = site->a0, a1 = site->a0 + site->w, d = site->d;
    float ins_l = pcg_room_walled(mask, PCG_ROOM_LEFT) ? rt : 0.0f;
    float ins_r = pcg_room_walled(mask, PCG_ROOM_RIGHT) ? rt : 0.0f;

    /*
     * The front and the back run along +a between the flank walls; the
     * flanks run along +c the room's whole depth, standing on their line
     * toward -a, as a wall takes its thickness toward its own +c
     */
    pcg_frame line;
    float r0, r1;

    if (side == PCG_ROOM_FRONT || side == PCG_ROOM_BACK) {
        float c = side == PCG_ROOM_FRONT ? d : rt;

        line = (pcg_frame){ .org = { 0.0f, 0.0f, c - rt }, .dir = { 1.0f, 0.0f } };
        r0 = a0 + ins_l;
        r1 = a1 - ins_r;
    } else {
        float a = side == PCG_ROOM_LEFT ? a0 + rt : a1;

        line = (pcg_frame){ .org = { a, 0.0f, 0.0f }, .dir = { 0.0f, 1.0f } };
        r0 = 0.0f;
        r1 = d;
    }

    float len = r1 - r0;
    const pcg_opening door = { .at = len * 0.5f, .w = p->door_w, .h = p->door_h };
    size_t nr_doors = !!(mask & PCG_ROOM_DOOR(side));

    /* a flank's doorway stays clear of the front and back walls' ends */
    if (nr_doors && (side == PCG_ROOM_LEFT || side == PCG_ROOM_RIGHT)) {
        float ins_b = pcg_room_walled(mask, PCG_ROOM_BACK) ? rt : 0.0f;
        float ins_f = pcg_room_walled(mask, PCG_ROOM_FRONT) ? rt : 0.0f;

        if (!(door.at - door.w * 0.5f >= ins_b && door.at + door.w * 0.5f <= len - ins_f))
            return CERR_INVALID_ARGUMENTS_REASON(.fmt = "room flank door runs into the front or back wall");
    }

    if (solid) {
        const pcg_wall_site ws = {
            .frame = room_frame(site, &line), .a0 = r0, .len = len, .h = site->h,
            .openings = &door, .nr_openings = nr_doors,
        };

        return emit ? _pcg_wall_emit(ctx, &p->wall, &ws) : pcg_wall_check(&p->wall, &ws);
    }

    /* the pane runs from the wall's start, in the middle of its thickness */
    pcg_frame pane = line;
    if (side == PCG_ROOM_FRONT || side == PCG_ROOM_BACK) {
        pane.org[0] = r0;
        pane.org[2] += rt * 0.5f;
    } else {
        pane.org[0] -= rt * 0.5f;
        pane.org[2] = r0;
    }

    const pcg_glazing_site gs = {
        .frame = room_frame(site, &pane), .len = len, .y0 = 0.0f, .y1 = site->h,
        .openings = &door, .nr_openings = nr_doors,
    };

    return emit ? _pcg_glazing_emit(ctx, &p->glass, &gs) : pcg_glazing_check(&p->glass, &gs);
}

/* the sign's site into @out, its doorway into @door; false for a room without a front or a label */
static bool room_sign(const pcg_room_params *p, const pcg_room_site *site, pcg_opening *door, pcg_sign_site *out)
{
    uint32_t mask = site->wall_mask;

    if (!site->label || !*site->label || !pcg_room_walled(mask, PCG_ROOM_FRONT))    return false;

    float rt = p->wall.t;
    float ins_l = pcg_room_walled(mask, PCG_ROOM_LEFT) ? rt : 0.0f;
    float ins_r = pcg_room_walled(mask, PCG_ROOM_RIGHT) ? rt : 0.0f;
    float r0 = site->a0 + ins_l, r1 = site->a0 + site->w - ins_r, len = r1 - r0;
    bool glass = !(mask & PCG_ROOM_SOLID(PCG_ROOM_FRONT));

    /* on the solid wall's face, or on the pane's */
    float face = glass ? site->d - rt * 0.5f : site->d;
    const pcg_frame line = { .org = { 0.0f, 0.0f, face }, .dir = { 1.0f, 0.0f } };

    *door = (pcg_opening){ .at = len * 0.5f, .w = p->door_w, .h = p->door_h };
    *out = (pcg_sign_site){
        .frame  = room_frame(site, &line),
        .a0     = r0,
        .len    = len,
        .door   = mask & PCG_ROOM_DOOR(PCG_ROOM_FRONT) ? door : nullptr,
        .text   = site->label,
        .glass  = glass,
    };

    return true;
}

/* the light in the middle of the room, hanging from the ceiling */
static pcg_fixture_site room_light(const pcg_room_site *site)
{
    float fa = (site->a0 + (site->a0 + site->w)) * 0.5f, fc = site->d * 0.5f;
    const pcg_frame center = { .org = { fa, 0.0f, fc }, .dir = { 1.0f, 0.0f } };

    return (pcg_fixture_site){
        .frame      = room_frame(site, &center),
        .ceiling    = site->ceiling,
    };
}

cerr pcg_room_check(const pcg_room_params *p, const pcg_room_site *site)
{
    if (!p || !site)    return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(room_site_check(p, site));

    for (int side = 0; side < PCG_ROOM_NR_SIDES; side++)    CERR_RET_CERR(room_wall(nullptr, p, site, side, false));

    pcg_fixture_site light = room_light(site);
    CERR_RET_CERR(pcg_fixture_check(&p->light, &light));

    pcg_opening door;
    pcg_sign_site sign;
    if (room_sign(p, site, &door, &sign))   CERR_RET_CERR(pcg_sign_check(&p->sign, &sign));

    return CERR_OK;
}

cerr _pcg_room_emit(const pcg_ctx *ctx, const pcg_room_params *p, const pcg_room_site *site)
{
    if (!ctx)   return CERR_INVALID_ARGUMENTS;
    CERR_RET_CERR(pcg_room_check(p, site));

    float rt = p->wall.t, a0 = site->a0, a1 = site->a0 + site->w, d = site->d;

    for (int side = 0; side < PCG_ROOM_NR_SIDES; side++)    CERR_RET_CERR(room_wall(ctx, p, site, side, true));

    /* wall to wall, inside where the walls would stand, charting the site frame's meters */
    pcg_ctx at = pcg_ctx_at(ctx, &site->frame);
    CERR_RET_CERR(pcg_box(&at, .lo = { a0 + rt, 0.0f, rt }, .hi = { a1 - rt, p->carpet_t, d - rt },
                          .face = PCG_BOX_ALL(&p->carpet)));

    pcg_fixture_site light = room_light(site);
    CERR_RET_CERR(_pcg_fixture_emit(ctx, &p->light, &light));

    pcg_opening door;
    pcg_sign_site sign;
    if (room_sign(p, site, &door, &sign))   CERR_RET_CERR(_pcg_sign_emit(ctx, &p->sign, &sign));

    if (!p->visit)  return CERR_OK;

    /* the hook's context has its frame at the interior's corner */
    const pcg_frame corner = { .org = { a0 + rt, 0.0f, rt }, .dir = { 1.0f, 0.0f } };
    pcg_frame inner = room_frame(site, &corner);
    pcg_ctx inside = pcg_ctx_at(ctx, &inner);
    const pcg_room_info info = {
        .w          = a1 - a0 - 2.0f * rt,
        .d          = d - 2.0f * rt,
        .h          = site->ceiling,
        .door_w     = p->door_w,
        .door_h     = p->door_h,
        .wall_mask  = site->wall_mask,
        .tag        = site->tag,
    };

    return p->visit(&inside, &info, p->visit_priv);
}
