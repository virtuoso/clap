/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_ANIMATOR_CHARACTER_ANIMS_H__
#define __CLAP_ANIMATOR_CHARACTER_ANIMS_H__

#include "animator.h"

/*
 * What a character's model plays in each of the character's states
 * (core/character.h): the transition table of the model's library, built
 * once per model by the scene loader and shared by every character of it.
 * The character asks for it with character_anims_enter().
 */

/**
 * character_anims_baked() - the default table, from the model's clips
 * @lib:    the model's library, with the clips under their semantic names
 *          ("idle", "motion", "jump_to_idle", ...; scene.c's "animations")
 * @fade:   the cross-fade every transition gets, seconds; 0 cuts
 *
 * One sequence per state transition, a lead-in played once and a loop:
 * idle -> moving is [motion_start, motion*], and so on. A transition whose
 * clips the model lacks is left out, so character_anims_enter() plays
 * nothing for it. The scene's "sequences" object is the same table spelled
 * out, see anim_lib_transitions_from_json().
 * Context: model setup, after the clips have their semantic names
 * Return: CERR_OK or CERR_NOMEM
 */
cerr character_anims_baked(anim_lib_t *lib, double fade);

/**
 * character_anims_pose() - the table for a rig that serves every state
 * @lib:    the model's library
 * @ani:    the rig, an anim_lib_pose() handle (bound on the entity by whoever
 *          owns the rig)
 *
 * Every state the character stays in plays @ani; the two it leaves on the
 * end callback, WAKING and JUMP_START, play a zero hold instead, so the
 * callback comes at the next update. A procedurally built body with no clips
 * walks, idles, jumps and falls on its gait this way.
 * Context: model setup
 * Return: CERR_OK or CERR_NOMEM
 */
cerr character_anims_pose(anim_lib_t *lib, anim_t ani);

#endif /* __CLAP_ANIMATOR_CHARACTER_ANIMS_H__ */
