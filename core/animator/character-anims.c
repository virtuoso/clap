// SPDX-License-Identifier: Apache-2.0
/*
 * The character's transition tables: the default, from the clip names its
 * models have always carried, one sequence per state transition, and the one
 * for a rig that poses the body in every state.
 */
#include "common.h"
#include "character.h"
#include "animator/character-anims.h"

/* [@lead_in once, @loop forever]; either may be left out */
static cres(anim_t) baked_seq(anim_lib_t *lib, const char *lead_in, const char *loop)
{
    /* the clips first: a row the model can't fill leaves nothing behind */
    anim_t in = lead_in ? CRES_RET_SCALAR(anim_lib_clip(lib, lead_in), anim_t) : 0;
    anim_t lp = loop ? CRES_RET_SCALAR(anim_lib_clip(lib, loop), anim_t) : 0;
    anim_t seq = CRES_RET_SCALAR(anim_lib_seq_make(lib), anim_t);

    if (lead_in)    CERR_RET_SCALAR(anim_lib_seq_push(lib, seq, in, false), anim_t);
    if (loop)       CERR_RET_SCALAR(anim_lib_seq_push(lib, seq, lp, true), anim_t);

    return cres_val(anim_t, seq);
}

/*
 * LEVITATING has no entry on purpose: the fall clip keeps playing through
 * it. START is a one-frame pose the character sleeps in; it restarts every
 * frame.
 */
cerr character_anims_baked(anim_lib_t *lib)
{
    static const struct {
        character_state to;
        character_state from;
        bool            any;        /* entered from anywhere */
        const char      *lead_in;   /* played once first, if set */
        const char      *loop;      /* then this forever, if set */
    } table[] = {
        { .to = CS_START,      .any = true,          .loop = "start" },
        { .to = CS_WAKING,     .any = true,          .lead_in = "start_to_idle" },
        { .to = CS_IDLE,       .from = CS_WAKING,    .loop = "idle" },
        { .to = CS_IDLE,       .from = CS_MOVING,    .lead_in = "motion_stop",    .loop = "idle" },
        { .to = CS_IDLE,       .from = CS_JUMPING,   .lead_in = "jump_to_idle",   .loop = "idle" },
        { .to = CS_IDLE,       .from = CS_FALLING,   .lead_in = "fall_to_idle",   .loop = "idle" },
        { .to = CS_IDLE,       .any = true,          .loop = "idle" },
        { .to = CS_MOVING,     .from = CS_IDLE,      .lead_in = "motion_start",   .loop = "motion" },
        { .to = CS_MOVING,     .any = true,          .lead_in = "jump_to_motion", .loop = "motion" },
        { .to = CS_JUMP_START, .from = CS_IDLE,      .lead_in = "idle_to_jump" },
        { .to = CS_JUMP_START, .from = CS_MOVING,    .lead_in = "motion_to_jump" },
        { .to = CS_JUMPING,    .any = true,          .loop = "jump" },
        { .to = CS_ASCENDING,  .any = true,          .loop = "fall" },
        { .to = CS_FALLING,    .any = true,          .loop = "fall" },
    };

    for (size_t i = 0; i < array_size(table); i++) {
        cres(anim_t) seq = baked_seq(lib, table[i].lead_in, table[i].loop);

        if (IS_CERR_CODE(seq, CERR_NOT_FOUND))  continue;
        if (IS_CERR(seq))                       return cerr_error_cres(seq);

        if (table[i].any)
            CERR_RET_CERR(anim_lib_transition_add_any(lib, table[i].to, seq.val, 0.0));
        else
            CERR_RET_CERR(anim_lib_transition_add(lib, table[i].to, table[i].from, seq.val, 0.0));
    }

    return CERR_OK;
}

cerr character_anims_pose(anim_lib_t *lib, anim_t ani)
{
    static const character_state stays[] = {
        CS_START, CS_IDLE, CS_MOVING, CS_JUMPING, CS_ASCENDING, CS_LEVITATING, CS_FALLING,
    };
    static const character_state waits[] = { CS_WAKING, CS_JUMP_START };
    anim_t beat = CRES_RET_CERR(anim_lib_hold(lib, 0.0));

    for (size_t i = 0; i < array_size(stays); i++)  CERR_RET_CERR(anim_lib_transition_add_any(lib, stays[i], ani, 0.0));
    for (size_t i = 0; i < array_size(waits); i++)
        CERR_RET_CERR(anim_lib_transition_add_any(lib, waits[i], beat, 0.0));

    return CERR_OK;
}
