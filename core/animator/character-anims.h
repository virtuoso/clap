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
 *
 * One sequence per state transition, a lead-in played once and a loop:
 * idle -> moving is [motion_start, motion*], and so on. A transition whose
 * clips the model lacks is left out, so character_anims_enter() plays
 * nothing for it. The scene's "sequences" object is the same table spelled
 * out, see anim_lib_transitions_from_json().
 * Context: model setup, after the clips have their semantic names
 * Return: CERR_OK or CERR_NOMEM
 */
cerr character_anims_baked(anim_lib_t *lib);

#endif /* __CLAP_ANIMATOR_CHARACTER_ANIMS_H__ */
