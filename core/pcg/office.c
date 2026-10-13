// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include <math.h>
#include "prng.h"
#include "pcg/internal.h"
#include "pcg/office.h"

void pcg_office_params_init(pcg_office_params *p)
{
    *p = (pcg_office_params){
        .min_size       = 2.2f,
        .corner_clear   = 0.3f,
        .wall_gap       = 0.1f,
        .monitor_slide  = 0.5f,
        .chair_off      = 0.75f,
        .chair_slide    = 0.2f,
        .chair_turn     = 0.9f,
        .solid_score    = 4,
        .back_score     = 2,
        .luck           = 2,
    };
    pcg_desk_params_init(&p->desk);
    pcg_chair_params_init(&p->chair);
    pcg_monitor_params_init(&p->monitor);
    pcg_corkboard_params_init(&p->board);
}

cerr pcg_office_check(const pcg_office_params *p)
{
    if (!p)     return CERR_INVALID_ARGUMENTS;

    const float dims[] = {
        p->min_size, p->corner_clear, p->wall_gap, p->monitor_slide, p->chair_off, p->chair_slide, p->chair_turn,
    };
    for (size_t i = 0; i < array_size(dims); i++)
        if (!pcg_nonnegative_finite(dims[i]))
            return CERR_INVALID_ARGUMENTS_REASON(.fmt = "office distance, slide or turn is negative or isn't finite");
    if (p->luck < 0)    return CERR_INVALID_ARGUMENTS_REASON(.fmt = "office luck is negative");

    const int weights[] = { p->solid_score, p->back_score, p->luck };
    for (size_t i = 0; i < array_size(weights); i++)
        if (weights[i] < -PCG_OFFICE_WEIGHT_MAX || weights[i] > PCG_OFFICE_WEIGHT_MAX)
            return CERR_INVALID_ARGUMENTS_REASON(.fmt = "office score or luck is out of range");

    /* the pieces' styles; their frames are the furnisher's, and always good */
    const pcg_frame f = PCG_FRAME_IDENTITY;
    CERR_RET_CERR(pcg_desk_check(&p->desk, &(pcg_desk_site){ .frame = f }));
    CERR_RET_CERR(pcg_chair_check(&p->chair, &(pcg_chair_site){ .frame = f }));
    CERR_RET_CERR(pcg_monitor_check(&p->monitor, &(pcg_monitor_site){ .frame = f }));

    return pcg_corkboard_check(&p->board, &(pcg_corkboard_site){ .frame = f });
}

/**
 * struct office_wall - a room side as the desk sees it, in the room's (a, c)
 * @org:    the wall's start on its face line
 * @run:    unit direction along the wall
 * @in:     unit direction into the room
 * @len:    the wall's length
 */
struct office_wall {
    vec2    org;
    vec2    run;
    vec2    in;
    float   len;
};

/* the interior's @side, its face line running from the interior's lower corner */
static struct office_wall office_wall_get(const pcg_room_info *info, int side)
{
    switch (side) {
        case PCG_ROOM_BACK:
            return (struct office_wall){ .run = { 1.0f, 0.0f }, .in = { 0.0f, 1.0f }, .len = info->w };
        case PCG_ROOM_FRONT:
            return (struct office_wall){
                .org = { 0.0f, info->d }, .run = { 1.0f, 0.0f }, .in = { 0.0f, -1.0f }, .len = info->w,
            };
        case PCG_ROOM_LEFT:
            return (struct office_wall){ .run = { 0.0f, 1.0f }, .in = { 1.0f, 0.0f }, .len = info->d };
        default:
            return (struct office_wall){
                .org = { info->w, 0.0f }, .run = { 0.0f, 1.0f }, .in = { -1.0f, 0.0f }, .len = info->d,
            };
    }
}

/*
 * The side the desk backs onto: walled and without a doorway, counting
 * a solid wall, the back wall and a dash of luck; CERR_NOT_FOUND if
 * there is none
 */
static cres(int) office_desk_side(const pcg_office_params *p, const pcg_room_info *info, prng *rng)
{
    uint32_t mask = info->wall_mask;
    int best = 0, best_score = INT_MIN;
    bool found = false;

    for (int side = 0; side < PCG_ROOM_NR_SIDES; side++) {
        /* every side draws its luck, so the stream moves on the same whatever the walls */
        int luck = (int)(prng_unit(rng) * p->luck);
        bool solid = mask & PCG_ROOM_SOLID(side);

        if (!pcg_room_walled(mask, side) || (mask & PCG_ROOM_DOOR(side)))   continue;

        int score = (solid ? p->solid_score : 0) + (side == PCG_ROOM_BACK ? p->back_score : 0) + luck;
        if (!found || score > best_score) {
            best_score = score;
            best = side;
            found = true;
        }
    }

    if (!found)     return cres_error(int, CERR_NOT_FOUND);

    return cres_val(int, best);
}

cerr pcg_office_furnish(const pcg_ctx *ctx, const pcg_room_info *info, void *priv)
{
    const pcg_office_params *p = priv;

    if (!ctx || !info || !p)    return CERR_INVALID_ARGUMENTS;
    if (!ctx->rng)              return CERR_INVALID_ARGUMENTS_REASON(.fmt = "office furnished without a random stream");
    CERR_RET_CERR(pcg_office_check(p));

    /* a stream per room, split before anything can skip the room */
    LOCAL_SET(prng, rng) = CRES_RET_CERR(prng_split(ctx->rng));

    if (info->w < p->min_size || info->d < p->min_size)     return CERR_OK;

    /* its only error: no wall to put the desk against, which leaves the room empty */
    int side = CRES_RET(office_desk_side(p, info, rng), return CERR_OK);

    /* the desk against the wall, along it at random, its ends clear of the corners */
    struct office_wall wall = office_wall_get(info, side);
    float margin = p->desk.w * 0.5f + p->corner_clear;
    float along = wall.len * 0.5f + (prng_unit(rng) - 0.5f) * (wall.len - 2.0f * margin);
    if (wall.len < 2.0f * margin)   along = wall.len * 0.5f;

    float off = p->desk.d * 0.5f + p->wall_gap;
    /* the desk's +c looks into the room */
    const pcg_frame desk = {
        .org = { wall.org[0] + wall.run[0] * along + wall.in[0] * off, 0.0f,
                 wall.org[1] + wall.run[1] * along + wall.in[1] * off },
        .dir = { wall.in[1], -wall.in[0] },
    };
    pcg_ctx at = pcg_ctx_at(ctx, &desk);

    CERR_RET_CERR(pcg_desk_emit(&at, &p->desk, .frame = PCG_FRAME_IDENTITY));

    /* the monitor on the desk, slid along it, looking the desk's way */
    float slide = (prng_unit(rng) - 0.5f) * p->monitor_slide;
    CERR_RET_CERR(pcg_monitor_emit(&at, &p->monitor,
                                   .frame = { .org = { slide, p->desk.h, 0.0f }, .dir = { 1.0f, 0.0f } }));

    /* the chair out in front, facing the desk give or take a turn */
    float out = p->chair_off + prng_unit(rng) * p->chair_slide;
    float turn = (prng_unit(rng) - 0.5f) * p->chair_turn;
    CERR_RET_CERR(pcg_chair_emit(&at, &p->chair,
                                 .frame = { .org = { 0.0f, 0.0f, out }, .dir = { -cosf(turn), -sinf(turn) } }));

    /* the corkboard on a solid wall over the desk */
    if (!(info->wall_mask & PCG_ROOM_SOLID(side)))  return CERR_OK;

    return pcg_corkboard_emit(&at, &p->board, .frame = { .org = { 0.0f, 0.0f, -off }, .dir = { 1.0f, 0.0f } });
}
