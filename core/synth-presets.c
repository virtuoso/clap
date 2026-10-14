// SPDX-License-Identifier: Apache-2.0
//
// Built-in synth patches: sound design that ships with the engine instead of
// as assets. The unpitched ones are noise driven by a looping note list, one
// note per gust or crackle, so the envelopes retrigger in an irregular
// rhythm without a modulator of their own.

#include <string.h>

#include "synth-presets.h"

/* Equal temperament at A4 = 440 Hz, as synth_pitch_frequency() computes it */
#define NOTE_C4         261.626f
#define NOTE_D4         293.665f
#define NOTE_E4         329.628f
#define NOTE_F4         349.228f
#define NOTE_G4         391.995f

/* The Lick at 160 bpm: an eighth lasts 187.5 ms */
#define LICK_EIGHTH     187.5f
#define LICK_QUARTER    375.0f

/** UNPITCHED - frequency of a noise note: noise ignores the pitch, but a zero frequency is a rest */
#define UNPITCHED       1.0f

static const synth_note wind_gusts[] = {
    { UNPITCHED, 3200.0f }, { UNPITCHED, 2100.0f }, { UNPITCHED, 4400.0f },
    { UNPITCHED, 1700.0f }, { UNPITCHED, 2900.0f },
};

static const synth_note crackles[] = {
    { UNPITCHED, 90.0f },  { UNPITCHED, 140.0f }, { UNPITCHED, 60.0f },  { UNPITCHED, 210.0f },
    { UNPITCHED, 75.0f },  { UNPITCHED, 330.0f }, { UNPITCHED, 50.0f },  { UNPITCHED, 120.0f },
    { UNPITCHED, 180.0f }, { UNPITCHED, 45.0f },  { UNPITCHED, 260.0f }, { UNPITCHED, 95.0f },
};

/* One burst: the note outlasts the envelopes, so the synth stops itself after it */
static const synth_note click_tick[] = { { UNPITCHED, 40.0f } };

static const synth_note the_lick[] = {
    { NOTE_D4, LICK_EIGHTH }, { NOTE_E4, LICK_EIGHTH }, { NOTE_F4, LICK_EIGHTH }, { NOTE_G4, LICK_EIGHTH },
    { NOTE_E4, LICK_QUARTER },
    { NOTE_C4, LICK_EIGHTH }, { NOTE_D4, LICK_QUARTER },
};

static const struct {
    const char      *name;
    synth_config    config;
} synth_presets[SYNTH_PRESET_NR] = {
    [SYNTH_PRESET_WIND] = {
        .name   = "wind",
        .config = {
            .oscillators    = { { .waveform = SYNTH_WAVE_PINK_NOISE, .gain = 1.0f } },
            .nr_oscillators = 1,
            .category       = SOUND_CAT_SFX,
            .filter         = { .cutoff = 250.0f, .resonance = 3.0f, .env_amount = 1.5f },
            .amp_env        = { .attack_ms = 1500.0f, .decay_ms = 2000.0f, .sustain = 0.6f, .release_ms = 2500.0f },
            .filter_env     = { .attack_ms = 900.0f, .decay_ms = 1800.0f, .sustain = 0.15f, .release_ms = 1200.0f },
            .gain           = 0.8f,
            .gate           = 0.7f,
            .nr_notes       = array_size(wind_gusts),
            .notes          = wind_gusts,
            .loop           = true,
        },
    },
    [SYNTH_PRESET_CLICK] = {
        .name   = "click",
        .config = {
            .oscillators    = { { .waveform = SYNTH_WAVE_WHITE_NOISE, .gain = 1.0f } },
            .nr_oscillators = 1,
            .category       = SOUND_CAT_SFX,
            .filter         = { .cutoff = 4000.0f, .resonance = 2.0f, .env_amount = 1.5f },
            .amp_env        = { .attack_ms = 0.0f, .decay_ms = 15.0f, .sustain = 0.0f, .release_ms = 5.0f },
            .filter_env     = { .attack_ms = 0.0f, .decay_ms = 10.0f, .sustain = 0.0f, .release_ms = 0.0f },
            .gain           = 0.5f,
            .gate           = 1.0f,
            .nr_notes       = array_size(click_tick),
            .notes          = click_tick,
        },
    },
    [SYNTH_PRESET_CRACKLING] = {
        .name   = "crackling",
        .config = {
            .oscillators    = { { .waveform = SYNTH_WAVE_WHITE_NOISE, .gain = 1.0f } },
            .nr_oscillators = 1,
            .category       = SOUND_CAT_SFX,
            .filter         = { .cutoff = 1500.0f, .resonance = 5.0f, .env_amount = 2.5f },
            .amp_env        = { .attack_ms = 0.0f, .decay_ms = 20.0f, .sustain = 0.0f, .release_ms = 10.0f },
            .filter_env     = { .attack_ms = 0.0f, .decay_ms = 15.0f, .sustain = 0.0f, .release_ms = 0.0f },
            .gain           = 0.5f,
            .gate           = 0.5f,
            .nr_notes       = array_size(crackles),
            .notes          = crackles,
            .loop           = true,
        },
    },
    [SYNTH_PRESET_THE_LICK] = {
        .name   = "the_lick",
        .config = {
            .oscillators    = {
                { .waveform = SYNTH_WAVE_SAWTOOTH, .detune = 0.0f,  .gain = 0.5f },
                { .waveform = SYNTH_WAVE_SQUARE,   .detune = -5.0f, .gain = 0.3f },
            },
            .nr_oscillators = 2,
            .category       = SOUND_CAT_MUSIC,
            .filter         = { .cutoff = 1000.0f, .resonance = 1.5f, .env_amount = 2.0f },
            .amp_env        = { .attack_ms = 5.0f, .decay_ms = 80.0f, .sustain = 0.7f, .release_ms = 120.0f },
            .filter_env     = { .attack_ms = 5.0f, .decay_ms = 250.0f, .sustain = 0.3f, .release_ms = 200.0f },
            .gain           = 0.5f,
            .gate           = 0.85f,
            .nr_notes       = array_size(the_lick),
            .notes          = the_lick,
        },
    },
};

cresp(synth_config) synth_preset_config(synth_preset preset)
{
    if (preset >= SYNTH_PRESET_NR)  return cresp_error(synth_config, CERR_INVALID_ARGUMENTS);

    return cresp_val(synth_config, &synth_presets[preset].config);
}

cresp(const_char) synth_preset_name(synth_preset preset)
{
    if (preset >= SYNTH_PRESET_NR)  return cresp_error(const_char, CERR_INVALID_ARGUMENTS);

    return cresp_val(const_char, synth_presets[preset].name);
}

cres(synth_preset) synth_preset_by_name(const char *name)
{
    if (!name)  return cres_error(synth_preset, CERR_INVALID_ARGUMENTS);

    for (synth_preset p = 0; p < SYNTH_PRESET_NR; p++)
        if (!strcmp(synth_presets[p].name, name))   return cres_val(synth_preset, p);

    return cres_error(synth_preset, CERR_NOT_FOUND);
}

cresp(synth) synth_preset_new(sound_context *ctx, synth_preset preset)
{
    const synth_config *config = CRES_RET_T(synth_preset_config(preset), synth);

    return ref_new_checked(synth, .ctx = ctx, .config = config);
}
