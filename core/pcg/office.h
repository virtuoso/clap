/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_OFFICE_H__
#define __CLAP_PCG_OFFICE_H__

#include "error.h"
#include "pcg/furniture.h"
#include "pcg/room.h"
#include "pcg/sink.h"

/**
 * define PCG_OFFICE_WEIGHT_MAX - the most a weight of the desk's pick counts for
 *
 * Luck is drawn through a float, which holds every whole number up to
 * this exactly, and a wall's three weights add up well inside an int.
 */
#define PCG_OFFICE_WEIGHT_MAX   (1 << 24)

/**
 * struct pcg_office_params - how an office is furnished
 * @desk:           the desk's style
 * @chair:          the chair's style
 * @monitor:        the monitor's style
 * @board:          the corkboard's style
 * @min_size:       rooms narrower or shallower than this stay empty
 * @corner_clear:   from the desk's ends to the corners of its wall, at
 *                  the least
 * @wall_gap:       from the wall out to the desk's back
 * @monitor_slide:  the monitor slides along the desk by up to half of
 *                  this either way off its middle
 * @chair_off:      from the desk's center out to the chair's pedestal,
 *                  at the least
 * @chair_slide:    how much further out than @chair_off the chair may
 *                  stand
 * @chair_turn:     the chair turns away from facing the desk by up to
 *                  half of this either way, radians
 * @solid_score:    what a solid wall counts for in picking the desk's
 *                  wall; a glass wall counts for nothing
 * @back_score:     what the back wall counts for
 * @luck:           every wall's count gains a random whole number below
 *                  this; ties go to the first side in &enum pcg_room_side
 *                  order
 *
 * @solid_score, @back_score and @luck stay within PCG_OFFICE_WEIGHT_MAX
 * either way, and @luck isn't negative.
 *
 * The desk, the monitor, the chair and the corkboard of one office,
 * placed by the rules of pcg_office_furnish().
 */
typedef struct pcg_office_params {
    pcg_desk_params         desk;
    pcg_chair_params        chair;
    pcg_monitor_params      monitor;
    pcg_corkboard_params    board;
    float                   min_size;
    float                   corner_clear;
    float                   wall_gap;
    float                   monitor_slide;
    float                   chair_off;
    float                   chair_slide;
    float                   chair_turn;
    int                     solid_score;
    int                     back_score;
    int                     luck;
} pcg_office_params;

/**
 * pcg_office_params_init() - a generic office's furnishing
 * @p:  the params to fill, every field
 *
 * The pieces' own defaults. Rooms under 2.2 m either way stay empty. The
 * desk keeps 0.3 m off the corners and 0.1 m off its wall, the monitor
 * slides up to 0.25 m either way, the chair stands 0.75 m to 0.95 m out
 * and turns up to 0.45 rad either way. A solid wall counts 4, the back
 * wall 2, and luck adds 0 or 1: the desk takes the back wall if it is
 * solid, any solid wall over glass, and the back wall over the sides.
 */
void pcg_office_params_init(pcg_office_params *p);

/**
 * pcg_office_check() - check an office's furnishing
 * @p:  the params
 *
 * Everything pcg_office_furnish() checks before it places anything: its
 * own fields, and every piece's style the way the piece's own check
 * checks it.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p, a distance,
 * slide or turn that is negative or isn't finite, a negative @luck, a
 * weight past PCG_OFFICE_WEIGHT_MAX either way, or the errors of the
 * pieces' checks
 */
cerr_check pcg_office_check(const pcg_office_params *p);

/**
 * pcg_office_furnish() - furnish a room as an office
 * @ctx:    the room's context, with its random stream
 * @info:   the room
 * @priv:   the &struct pcg_office_params, const
 *
 * A &pcg_room_visit_fn. Rooms narrower or shallower than the minimum
 * size stay empty. Otherwise the desk backs onto a walled side without a
 * doorway, the one whose solid wall, back wall and luck count for the
 * most; a room without such a side stays empty. The desk stands its gap
 * off that wall, at a random spot along it that keeps its ends clear of
 * the corners, or in the middle of a wall too short for that. The
 * monitor stands on the desk, slid along it at random. The chair stands
 * in front of the desk at a random distance, turned toward it at a
 * random angle. The corkboard hangs on the wall over the desk, if that
 * wall is solid. Every piece is turned with the wall, its yaw baked into
 * the vertices.
 *
 * Each room draws from its own stream, split off @ctx->rng: the
 * furniture of a room depends on the stream's seed and the order the
 * rooms are visited in, not on what other rooms draw.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @ctx, @info or
 * @priv, or a @ctx without a random stream; the pcg_office_check()
 * errors; the errors of the pieces' emits, which can leave the pieces
 * placed before them behind
 */
cerr_check pcg_office_furnish(const pcg_ctx *ctx, const pcg_room_info *info, void *priv);

#endif /* __CLAP_PCG_OFFICE_H__ */
