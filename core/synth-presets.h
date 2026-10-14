/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SYNTH_PRESETS_H__
#define __CLAP_SYNTH_PRESETS_H__

#include "sound.h"

/**
 * enum synth_preset - built-in synth patches
 * @SYNTH_PRESET_WIND:      pink noise gusting through a resonant lowpass; loops
 * @SYNTH_PRESET_CLICK:     a UI tick: one shot that stops itself, replayed
 *                          with synth_start()
 * @SYNTH_PRESET_CRACKLING: a fire: a loop of short, irregular noise bursts
 * @SYNTH_PRESET_THE_LICK:  D E F G E C D on a detuned saw and square, once
 * @SYNTH_PRESET_NR:        number of presets
 */
typedef enum synth_preset {
    SYNTH_PRESET_WIND = 0,
    SYNTH_PRESET_CLICK,
    SYNTH_PRESET_CRACKLING,
    SYNTH_PRESET_THE_LICK,
    SYNTH_PRESET_NR,
} synth_preset;

cres_ret(synth_preset);
cresp_ret(synth_config, const);

/**
 * synth_preset_config() - the patch of a preset
 * @preset: preset id
 *
 * Return: the preset's configuration, its notes included, or
 * CERR_INVALID_ARGUMENTS for an id out of range
 */
cresp(synth_config) synth_preset_config(synth_preset preset);

/**
 * synth_preset_name() - the name of a preset
 * @preset: preset id
 *
 * Return: the name synth_preset_by_name() resolves, or
 * CERR_INVALID_ARGUMENTS for an id out of range
 */
cresp(const_char) synth_preset_name(synth_preset preset);

/**
 * synth_preset_by_name() - look a preset up by name
 * @name:   "wind", "click", "crackling" or "the_lick"
 *
 * Return: the preset id, or CERR_NOT_FOUND
 */
cres(synth_preset) synth_preset_by_name(const char *name);

/**
 * synth_preset_new() - create a synth from a preset
 * @ctx:    sound context
 * @preset: preset id
 *
 * The synth is created stopped, like any other; synth_start() plays it.
 *
 * Return: the synth, or the error creating it
 */
cresp(synth) synth_preset_new(sound_context *ctx, synth_preset preset);

#endif /* __CLAP_SYNTH_PRESETS_H__ */
