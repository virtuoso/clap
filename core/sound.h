/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SOUND_H__
#define __CLAP_SOUND_H__

#include "object.h"
#include "util.h"

typedef struct sound_context sound_context;
typedef struct sound sound;
typedef struct sfx sfx;
typedef struct sound_effect sound_effect;
typedef struct sound_effect_chain sound_effect_chain;
typedef struct synth synth;

struct clap_context;

cresp_ret(sound_context);

/**
 * enum sound_category - volume bucket a sound belongs to
 * @SOUND_CAT_NONE:     ungrouped, plays at its per-sound gain only
 * @SOUND_CAT_MUSIC:    scaled by master music volume
 * @SOUND_CAT_SFX:      scaled by master SFX volume (sfx_new assigns this)
 */
typedef enum sound_category {
    SOUND_CAT_NONE = 0,
    SOUND_CAT_MUSIC,
    SOUND_CAT_SFX,
} sound_category;

typedef struct sfx_container {
    struct list list;
    void        (*on_add)(sound *, void *);
    void        *data;
} sfx_container;

DEFINE_REFCLASS_INIT_OPTIONS(sound,
    const char      *name;
    sound_context   *ctx;
    sound_category  category;
);
DECLARE_REFCLASS(sound);

cresp(sound_context) sound_init(struct clap_context *clap_ctx);
void sound_done(sound_context *ctx);
float sound_get_gain(sound *sound);
void sound_set_gain(sound *sound, float gain);
void sound_set_looping(sound *sound, bool looping);
void sound_play(sound *sound);

/**
 * sound_set_master_volume() - set the master gain for a category
 * @ctx:        sound context
 * @category:   %SOUND_CAT_MUSIC or %SOUND_CAT_SFX
 * @volume:     linear gain in [0.0, 1.0]
 *
 * Updates the stored master for @category and re-applies per-sound gain on
 * every live sound in that category, so the change takes effect immediately.
 */
void sound_set_master_volume(sound_context *ctx, sound_category category, float volume);

/**
 * sound_get_master_volume() - read the master gain for a category
 * @ctx:        sound context
 * @category:   %SOUND_CAT_MUSIC or %SOUND_CAT_SFX
 *
 * Return: stored linear gain, or 1.0 if @category is %SOUND_CAT_NONE.
 */
float sound_get_master_volume(sound_context *ctx, sound_category category);

/**
 * sound_effect_chain_enable() - enable or disable effect chain
 * @chain:  chain to modify
 * @enable: true to enable, false to disable
 */
void sound_effect_chain_enable(sound_effect_chain *chain, bool enable);

/**
 * sound_set_effect_chain() - attach effect chain to sound
 * @sound:  sound to modify
 * @chain:  chain to attach (NULL to detach)
 */
void sound_set_effect_chain(sound *sound, sound_effect_chain *chain);

typedef enum {
    SOUND_EFFECT_REVERB,
    SOUND_EFFECT_EQ,
    SOUND_EFFECT_COMPRESSOR,
    SOUND_EFFECT_DELAY,
} sound_effect_type;

typedef enum reverb_type {
    REVERB_SMALL_ROOM,
    REVERB_HALL,
} reverb_type;

DEFINE_REFCLASS_INIT_OPTIONS(sound_effect,
    sound_context       *ctx;
    sound_effect_type   type;
    reverb_type         reverb_type;
    float               room_size;
    float               damping;
    float               wet_dry;
    float               delay_ms[2];
    float               feedback;
);
DECLARE_REFCLASS(sound_effect);

DEFINE_REFCLASS_INIT_OPTIONS(sound_effect_chain,
    sound_context   *ctx;
);
DECLARE_REFCLASS(sound_effect_chain);

/**
 * sound_effect_chain_add() - add effect to chain
 * @chain:  chain to modify
 * @effect: effect to add
 */
void sound_effect_chain_add(sound_effect_chain *chain, sound_effect *effect);

/**
 * sound_effect_chain_remove() - remove effect from chain
 * @chain:  chain to modify
 * @effect: effect to remove
 */
void sound_effect_chain_remove(sound_effect_chain *chain, sound_effect *effect);

cresp_ret(sfx);
void sfx_container_init(sfx_container *sfxc);
void sfx_container_clearout(sfx_container *sfxc);
cresp(sfx) sfx_new(sfx_container *sfxc, const char *name, const char *file, sound_context *ctx);
sfx *sfx_get(sfx_container *sfxc, const char *name);
void sfx_play(sfx *sfx);
void sfx_play_by_name(sfx_container *sfxc, const char *name);

/****************************************************************************
 * Subtractive synthesizer
 ****************************************************************************/

/**
 * enum synth_waveform - oscillator waveform
 * @SYNTH_WAVE_SINE:        sine
 * @SYNTH_WAVE_SQUARE:      square
 * @SYNTH_WAVE_TRIANGLE:    triangle
 * @SYNTH_WAVE_SAWTOOTH:    sawtooth
 */
typedef enum synth_waveform {
    SYNTH_WAVE_SINE = 0,
    SYNTH_WAVE_SQUARE,
    SYNTH_WAVE_TRIANGLE,
    SYNTH_WAVE_SAWTOOTH,
} synth_waveform;

#define SYNTH_MAX_OSCILLATORS 4

/*
 * Cutoff range the lowpass is kept in: the bilinear transform warps the
 * response towards Nyquist, where the filter stops being a lowpass, and a
 * cutoff at DC makes the coefficients degenerate
 */
#define SYNTH_CUTOFF_MIN_HZ         10.0f
#define SYNTH_CUTOFF_MAX_RATIO      0.45f

/** SYNTH_DETUNE_MAX_CENTS - detune range in cents: an octave either way */
#define SYNTH_DETUNE_MAX_CENTS      1200.0f

/**
 * struct synth_oscillator_config - one oscillator of a synth
 * @waveform:   waveform to generate
 * @frequency:  base frequency in Hz, (0, Nyquist): the pitch the oscillator
 *              plays when the synth has neither notes nor a
 *              synth_config::next_note callback
 * @detune:     offset in cents from the base or note frequency, within
 *              %SYNTH_DETUNE_MAX_CENTS either way
 * @gain:       linear gain the oscillator contributes to the mix
 */
typedef struct synth_oscillator_config {
    synth_waveform  waveform;
    float           frequency;
    float           detune;
    float           gain;
} synth_oscillator_config;

/**
 * struct synth_envelope_config - ADSR envelope
 * @attack_ms:  time to rise from the current level to 1.0
 * @decay_ms:   time to fall from 1.0 to @sustain
 * @sustain:    level held while the note is gated, [0.0, 1.0]
 * @release_ms: time to fall from the current level to 0.0 once the gate ends
 */
typedef struct synth_envelope_config {
    float   attack_ms;
    float   decay_ms;
    float   sustain;
    float   release_ms;
} synth_envelope_config;

/**
 * struct synth_filter_config - resonant lowpass filter
 * @cutoff:     cutoff frequency in Hz with the filter envelope at 0.0, from
 *              %SYNTH_CUTOFF_MIN_HZ to %SYNTH_CUTOFF_MAX_RATIO of the sample rate
 * @resonance:  filter Q; 0.707 is flat, higher values peak at the cutoff
 * @env_amount: octaves added to @cutoff with the filter envelope at 1.0
 */
typedef struct synth_filter_config {
    float   cutoff;
    float   resonance;
    float   env_amount;
} synth_filter_config;

/**
 * struct synth_note - one note of a sequence
 * @frequency:      pitch in Hz, (0, Nyquist); 0.0 is a rest. A pitch outside
 *                  the range fails synth creation in synth_config::notes and
 *                  is played as a rest when it comes from the callback
 * @duration_ms:    length of the note, or of the rest, > 0; from the callback,
 *                  anything else lasts one frame
 */
typedef struct synth_note {
    float   frequency;
    float   duration_ms;
} synth_note;

/**
 * synth_next_note_fn - sequence callback
 * @note:   the note to fill in
 * @data:   synth_config::next_note_data
 *
 * Called from the audio thread when the previous note runs out, so it must
 * neither block nor allocate.
 *
 * Return: true if @note is filled in, false when the sequence is over
 */
typedef bool (*synth_next_note_fn)(synth_note *note, void *data);

/**
 * struct synth_config - subtractive synth patch
 * @oscillators:    oscillators summed into the filter
 * @nr_oscillators: entries in @oscillators, [1, %SYNTH_MAX_OSCILLATORS]
 * @filter:         lowpass filter the oscillator mix goes through
 * @amp_env:        amplitude envelope
 * @filter_env:     cutoff envelope, scaled by synth_filter_config::env_amount
 * @gain:           output gain
 * @gate:           fraction of a note's duration the envelopes are held before
 *                  they release, (0.0, 1.0]; used with @notes or @next_note
 * @nr_notes:       entries in @notes
 * @notes:          a sequence the synth plays by itself, from the top on every
 *                  synth_start(); exclusive with @next_note
 * @next_note:      sequence callback; with neither this nor @notes the
 *                  oscillators play their base frequencies until synth_stop()
 * @next_note_data: passed to @next_note
 * @loop:           whether @notes wrap around instead of ending
 */
typedef struct synth_config {
    synth_oscillator_config oscillators[SYNTH_MAX_OSCILLATORS];
    unsigned int            nr_oscillators;
    synth_filter_config     filter;
    synth_envelope_config   amp_env;
    synth_envelope_config   filter_env;
    float                   gain;
    float                   gate;
    unsigned int            nr_notes;
    const synth_note        *notes;
    synth_next_note_fn      next_note;
    void                    *next_note_data;
    bool                    loop;
} synth_config;

/*
 * The patch is copied at creation; the notes, the callback and its data have
 * to outlive the synth. A value outside the ranges documented above, a
 * non-finite one included, fails creation with CERR_INVALID_ARGUMENTS and a
 * reason naming the field. The synth is created stopped: synth_start() begins
 * the sequence.
 */
DEFINE_REFCLASS_INIT_OPTIONS(synth,
    sound_context       *ctx;
    const synth_config  *config;
);
DECLARE_REFCLASS(synth);
DECLARE_CLEANUP(synth);

/**
 * synth_start() - start rendering
 * @synth:  synth to start
 *
 * A stopped synth restarts its sequence with the next note the callback
 * returns; a synth that had no callback resumes its base frequencies.
 */
void synth_start(synth *synth);

/**
 * synth_stop() - stop rendering
 * @synth:  synth to stop
 *
 * Silences the synth immediately, release tails included.
 */
void synth_stop(synth *synth);

/**
 * synth_is_playing() - whether the synth's node is started
 * @synth:  synth to query
 *
 * Return: true between synth_start() and synth_stop()
 */
bool synth_is_playing(synth *synth);

/** SYNTH_TUNING_A440 - concert pitch: A4 at 440 Hz */
#define SYNTH_TUNING_A440 440.0f

/**
 * synth_midi_frequency() - frequency of a MIDI note in equal temperament
 * @midi:   MIDI note number, [0, 127]; 69 is A4
 * @tuning: frequency of A4 in Hz; 0.0 selects %SYNTH_TUNING_A440
 *
 * Return: frequency in Hz, or CERR_INVALID_ARGUMENTS
 */
cres(float) synth_midi_frequency(int midi, float tuning);

/**
 * synth_pitch_frequency() - frequency of a named pitch in equal temperament
 * @pitch:  scientific pitch notation: a letter A-G, any number of '#' or 'b'
 *          accidentals and an octave number, "C4" being middle C (MIDI 60),
 *          "A4" the tuning pitch and "C-1" MIDI 0
 * @tuning: frequency of A4 in Hz; 0.0 selects %SYNTH_TUNING_A440
 *
 * Return: frequency in Hz, CERR_PARSE_FAILED for a malformed name or
 * CERR_INVALID_ARGUMENTS for a pitch outside the MIDI range
 */
cres(float) synth_pitch_frequency(const char *pitch, float tuning);

/**
 * synth_note_duration_ms() - length of a note at a tempo
 * @bpm:        tempo in quarter notes per minute
 * @duration:   note value in standard notation: 1.0 is a whole note, 0.25 a
 *              quarter, 0.375 a dotted quarter
 *
 * Return: duration in milliseconds, or CERR_INVALID_ARGUMENTS
 */
cres(float) synth_note_duration_ms(float bpm, double duration);

#ifdef CLAP_TESTS
/**
 * sound_init_headless() - a sound context without a device
 * @sample_rate:    engine sample rate
 * @channels:       engine channel count
 *
 * The engine renders only when sound_read_frames() pulls from it, so the
 * tests get deterministic output. Freed with sound_done().
 *
 * Return: the context, CERR_NOMEM or CERR_INITIALIZATION_FAILED
 */
cresp(sound_context) sound_init_headless(unsigned int sample_rate, unsigned int channels);

/**
 * sound_read_frames() - render the engine's output
 * @ctx:    a headless sound context
 * @out:    interleaved f32 frames, @frames of them
 * @frames: frames to render
 *
 * Return: the number of frames rendered
 */
size_t sound_read_frames(sound_context *ctx, float *out, size_t frames);
#endif /* CLAP_TESTS */

#endif /* __CLAP_SOUND_H__ */
