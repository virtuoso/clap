// SPDX-License-Identifier: Apache-2.0
/*
 * Synth tests: the pitch and duration helpers, patch validation, offline
 * rendering through a headless engine and patch changes on a playing synth.
 * sound.c is compiled in whole, so the librarian it decodes files with is
 * stubbed out: no test loads one.
 */
#include <math.h>
#include <stdlib.h>
#include "librarian.h"
#include "logger.h"
#include "object.h"
#include "sound.h"
#include "synth-presets.h"
#include "test.h"
#include "util.h"

struct lib_handle *lib_read_file(enum res_type type, const char *name, void **buf, size_t *szp)
{
    return nullptr;
}

void cleanup__lib_handlep(lib_handle **p)
{
}

#define SAMPLE_RATE 48000
#define CHANNELS    2

/* A headless context lives in a scope and is torn down with sound_done() */
static void cleanup__headlessp(sound_context **p)
{
    if (*p)     sound_done(*p);
}

#define LOCAL_HEADLESS(x) LOCAL_SET_(sound_context *, headless, x)

static bool near(float a, float b, float tol)
{
    return fabsf(a - b) <= tol;
}

static int helpers_test(void)
{
    /* equal temperament at A4 = 440 Hz: 69 is A4, 60 middle C, 0 the bottom of the range */
    static const struct { int midi; float hz; } midi[] = {
        { 69, 440.0f }, { 60, 261.626f }, { 0, 8.176f }, { 127, 12543.854f },
    };
    for (size_t i = 0; i < array_size(midi); i++) {
        cres(float) r = synth_midi_frequency(midi[i].midi, 0.0f);
        if (IS_CERR(r) || !near(r.val, midi[i].hz, 0.01f)) {
            err("midi %d: expected %f\n", midi[i].midi, midi[i].hz);
            return EXIT_FAILURE;
        }
    }

    cres(float) tuned = synth_midi_frequency(69, 432.0f);
    if (IS_CERR(tuned) || !near(tuned.val, 432.0f, 0.001f)) return EXIT_FAILURE;

    static const struct { int midi; float tuning; } bad_midi[] = {
        { -1, 0.0f }, { 128, 0.0f }, { 69, -1.0f }, { 69, NAN },
    };
    for (size_t i = 0; i < array_size(bad_midi); i++) {
        cres(float) r = synth_midi_frequency(bad_midi[i].midi, bad_midi[i].tuning);
        if (!IS_CERR_CODE(r, CERR_INVALID_ARGUMENTS)) {
            err("midi %d at %f accepted\n", bad_midi[i].midi, bad_midi[i].tuning);
            return EXIT_FAILURE;
        }
    }

    /* scientific pitch notation with sharps, flats and the octave sign */
    static const struct { const char *name; float hz; } names[] = {
        { "A4", 440.0f },   { "C4", 261.626f },   { "Bb3", 233.082f }, { "C#5", 554.365f },
        { "c-1", 8.176f },  { "G9", 12543.854f }, { "Cbb4", 233.082f },
    };
    for (size_t i = 0; i < array_size(names); i++) {
        cres(float) r = synth_pitch_frequency(names[i].name, 0.0f);
        if (IS_CERR(r) || !near(r.val, names[i].hz, 0.01f)) {
            err("pitch %s: expected %f\n", names[i].name, names[i].hz);
            return EXIT_FAILURE;
        }
    }

    static const char *const malformed[] = { "H4", "A", "A#", "A4x", " A4", "A+4", "" };
    for (size_t i = 0; i < array_size(malformed); i++) {
        cres(float) r = synth_pitch_frequency(malformed[i], 0.0f);
        if (!IS_CERR_CODE(r, CERR_PARSE_FAILED)) {
            err("pitch '%s' parsed\n", malformed[i]);
            return EXIT_FAILURE;
        }
    }

    /* outside MIDI, including octaves that would wrap the note number if they reached the int arithmetic */
    static const char *const out_of_range[] = { "G#9", "Cb-1", "C10", "C4294967300", "C-4294967300" };
    for (size_t i = 0; i < array_size(out_of_range); i++) {
        cres(float) r = synth_pitch_frequency(out_of_range[i], 0.0f);
        if (!IS_CERR_CODE(r, CERR_INVALID_ARGUMENTS)) {
            err("pitch '%s' accepted\n", out_of_range[i]);
            return EXIT_FAILURE;
        }
    }

    static const struct { float bpm; double value; float ms; } durations[] = {
        { 120.0f, 0.25, 500.0f }, { 90.0f, 0.375, 1000.0f }, { 60.0f, 1.0, 4000.0f },
    };
    for (size_t i = 0; i < array_size(durations); i++) {
        cres(float) r = synth_note_duration_ms(durations[i].bpm, durations[i].value);
        if (IS_CERR(r) || !near(r.val, durations[i].ms, 0.001f)) {
            err("%f at %f bpm: expected %f ms\n", durations[i].value, durations[i].bpm, durations[i].ms);
            return EXIT_FAILURE;
        }
    }

    static const struct { float bpm; double value; } bad_durations[] = {
        { 0.0f, 0.25 }, { -120.0f, 0.25 }, { 120.0f, 0.0 }, { 120.0f, -1.0 },
        { NAN, 0.25 }, { INFINITY, 0.25 }, { 120.0f, NAN }, { 120.0f, INFINITY },
    };
    for (size_t i = 0; i < array_size(bad_durations); i++) {
        cres(float) r = synth_note_duration_ms(bad_durations[i].bpm, bad_durations[i].value);
        if (!IS_CERR_CODE(r, CERR_INVALID_ARGUMENTS)) {
            err("%f at %f bpm accepted\n", bad_durations[i].value, bad_durations[i].bpm);
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

/* A patch every check passes: one saw into a flat filter, envelopes that hold */
static const synth_config base_config = {
    .oscillators    = { { .waveform = SYNTH_WAVE_SAWTOOTH, .frequency = 220.0f, .gain = 1.0f } },
    .nr_oscillators = 1,
    .filter         = { .cutoff = 1000.0f, .resonance = 0.707f },
    .amp_env        = { .sustain = 1.0f },
    .filter_env     = { .sustain = 1.0f },
    .gain           = 1.0f,
};

static bool decline(synth_note *note, void *data)
{
    return false;
}

/* Whether creation refuses @config, dropping the synth if it doesn't */
static bool rejects(sound_context *ctx, const synth_config *config)
{
    cresp(synth) r = ref_new_checked(synth, .ctx = ctx, .config = config);
    if (IS_CERR(r))     return true;

    ref_put(r.val);

    return false;
}

#define REJECTS(...) do { \
    synth_config c = base_config; \
    __VA_ARGS__; \
    if (!rejects(ctx, &c)) { \
        err("accepted: %s\n", #__VA_ARGS__); \
        return EXIT_FAILURE; \
    } \
} while (0)

#define ACCEPTS(...) do { \
    synth_config c = base_config; \
    __VA_ARGS__; \
    if (rejects(ctx, &c)) { \
        err("rejected: %s\n", #__VA_ARGS__); \
        return EXIT_FAILURE; \
    } \
} while (0)

static int config_test(void)
{
    LOCAL_HEADLESS(ctx) = CRES_RET(sound_init_headless(SAMPLE_RATE, CHANNELS), return EXIT_FAILURE);

    ACCEPTS();
    REJECTS(c.nr_oscillators = 0);
    REJECTS(c.nr_oscillators = SYNTH_MAX_OSCILLATORS + 1);
    REJECTS(c.oscillators[0].waveform = SYNTH_WAVE_PINK_NOISE + 1);
    REJECTS(c.oscillators[0].gain = -1.0f);
    REJECTS(c.oscillators[0].gain = NAN);
    REJECTS(c.oscillators[0].frequency = 0.0f);
    REJECTS(c.oscillators[0].frequency = SAMPLE_RATE / 2.0f);
    REJECTS(c.oscillators[0].frequency = INFINITY);
    REJECTS(c.oscillators[0].detune = 2.0f * SYNTH_DETUNE_MAX_CENTS);
    REJECTS(c.oscillators[0].detune = NAN);
    /* noise has no pitch to check */
    ACCEPTS(c.oscillators[0].waveform = SYNTH_WAVE_WHITE_NOISE; c.oscillators[0].frequency = 0.0f);
    REJECTS(c.filter.cutoff = SYNTH_CUTOFF_MIN_HZ / 2.0f);
    REJECTS(c.filter.cutoff = SAMPLE_RATE * SYNTH_CUTOFF_MAX_RATIO + 1.0f);
    REJECTS(c.filter.resonance = 0.0f);
    REJECTS(c.filter.env_amount = INFINITY);
    REJECTS(c.amp_env.attack_ms = -1.0f);
    REJECTS(c.amp_env.sustain = 1.5f);
    REJECTS(c.filter_env.decay_ms = NAN);
    REJECTS(c.gain = -0.5f);
    REJECTS(c.category = SOUND_CAT_SFX + 1);

    /* a sequence: a note list and a callback are exclusive, a list needs a gate and sane notes */
    static const synth_note notes[] = { { 440.0f, 100.0f }, { 0.0f, 50.0f } };
    static const synth_note bad_duration[] = { { 440.0f, -1.0f } };
    static const synth_note bad_pitch[] = { { NAN, 100.0f } };
    static const synth_note high_pitch[] = { { SAMPLE_RATE, 100.0f } };

    ACCEPTS(c.notes = notes; c.nr_notes = array_size(notes); c.gate = 1.0f);
    ACCEPTS(c.next_note = decline; c.gate = 0.5f);
    REJECTS(c.notes = notes; c.nr_notes = 0; c.gate = 1.0f);
    REJECTS(c.nr_notes = 1; c.gate = 1.0f);
    REJECTS(c.notes = notes; c.nr_notes = array_size(notes));
    REJECTS(c.notes = notes; c.nr_notes = array_size(notes); c.gate = 1.5f);
    REJECTS(c.notes = notes; c.nr_notes = array_size(notes); c.gate = 1.0f; c.next_note = decline);
    REJECTS(c.notes = bad_duration; c.nr_notes = 1; c.gate = 1.0f);
    REJECTS(c.notes = bad_pitch; c.nr_notes = 1; c.gate = 1.0f);
    REJECTS(c.notes = high_pitch; c.nr_notes = 1; c.gate = 1.0f);

    return EXIT_SUCCESS;
}

/* Pull @frames of engine output in @block-frame reads; the peak absolute sample, or -1.0 on a short read */
static float render(sound_context *ctx, float *out, size_t frames, size_t block)
{
    for (size_t done = 0; done < frames; ) {
        size_t n = min(block, frames - done);

        if (sound_read_frames(ctx, out + done * CHANNELS, n) != n)  return -1.0f;

        done += n;
    }

    float peak = 0.0f;
    for (size_t i = 0; i < frames * CHANNELS; i++)  peak = fmaxf(peak, fabsf(out[i]));

    return peak;
}

/* The lick on a fresh engine, pulled in @block-frame reads */
static int render_lick(float *out, size_t frames, size_t block)
{
    LOCAL_HEADLESS(ctx) = CRES_RET(sound_init_headless(SAMPLE_RATE, CHANNELS), return EXIT_FAILURE);
    LOCAL_SET(synth, s) = CRES_RET(synth_preset_new(ctx, SYNTH_PRESET_THE_LICK), return EXIT_FAILURE);

    synth_start(s);
    if (render(ctx, out, frames, block) <= 0.0f)    return EXIT_FAILURE;

    /* the lick is under two seconds: the sequence is over and the node has stopped itself */
    if (synth_is_playing(s))                        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/*
 * The same patch renders the same samples whatever the size of the reads
 * pulling them. Compared as floats: the node stops itself at a read boundary,
 * before which a silent envelope leaves negative zeros in the output
 */
static int block_size_test(void)
{
    const size_t frames = 2 * SAMPLE_RATE;
    LOCAL_SET(float, a) = mem_alloc(frames * CHANNELS * sizeof(float));
    LOCAL_SET(float, b) = mem_alloc(frames * CHANNELS * sizeof(float));
    if (!a || !b)   return EXIT_FAILURE;

    if (render_lick(a, frames, 480) || render_lick(b, frames, 1000))    return EXIT_FAILURE;

    for (size_t i = 0; i < frames * CHANNELS; i++)
        if (a[i] != b[i]) {
            err("renders differ at sample %zu: %f vs %f\n", i, a[i], b[i]);
            return EXIT_FAILURE;
        }

    return EXIT_SUCCESS;
}

/* A one-shot stops its node after the burst; synth_start() replays it; a drone plays until synth_stop() */
static int lifecycle_test(void)
{
    const size_t frames = SAMPLE_RATE / 2;
    LOCAL_HEADLESS(ctx) = CRES_RET(sound_init_headless(SAMPLE_RATE, CHANNELS), return EXIT_FAILURE);
    LOCAL_SET(float, out) = mem_alloc(frames * CHANNELS * sizeof(float));
    if (!out)   return EXIT_FAILURE;

    LOCAL_SET(synth, click) = CRES_RET(synth_preset_new(ctx, SYNTH_PRESET_CLICK), return EXIT_FAILURE);
    if (synth_is_playing(click))                                return EXIT_FAILURE;

    synth_start(click);
    if (!synth_is_playing(click))                               return EXIT_FAILURE;
    if (render(ctx, out, frames, 480) <= 0.0f)                  return EXIT_FAILURE;
    if (synth_is_playing(click))                                return EXIT_FAILURE;

    /* retrigger: the burst plays again from the top */
    synth_start(click);
    if (!synth_is_playing(click))                               return EXIT_FAILURE;
    if (render(ctx, out, frames, 480) <= 0.0f)                  return EXIT_FAILURE;
    if (synth_is_playing(click))                                return EXIT_FAILURE;

    LOCAL_SET(synth, drone) = CRES_RET(ref_new_checked(synth, .ctx = ctx, .config = &base_config),
                                       return EXIT_FAILURE);
    synth_start(drone);
    if (render(ctx, out, frames, 480) <= 0.0f)                  return EXIT_FAILURE;
    if (!synth_is_playing(drone))                               return EXIT_FAILURE;

    synth_stop(drone);
    if (synth_is_playing(drone))                                return EXIT_FAILURE;
    if (render(ctx, out, frames, 480) != 0.0f)                  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* A setter reaches a playing synth, a rejected one leaves the patch alone, a waveform can't change */
static int live_patch_test(void)
{
    const size_t frames = SAMPLE_RATE / 10;
    LOCAL_HEADLESS(ctx) = CRES_RET(sound_init_headless(SAMPLE_RATE, CHANNELS), return EXIT_FAILURE);
    LOCAL_SET(float, out) = mem_alloc(frames * CHANNELS * sizeof(float));
    if (!out)   return EXIT_FAILURE;

    LOCAL_SET(synth, drone) = CRES_RET(ref_new_checked(synth, .ctx = ctx, .config = &base_config),
                                       return EXIT_FAILURE);
    synth_start(drone);
    if (render(ctx, out, frames, 480) <= 0.0f)                  return EXIT_FAILURE;

    /* the audio thread takes the muted patch at its next chunk */
    cerr err = synth_set_gain(drone, 0.0f);
    if (IS_CERR(err))                                           return EXIT_FAILURE;
    if (render(ctx, out, frames, 480) != 0.0f)                  return EXIT_FAILURE;

    err = synth_set_gain(drone, -1.0f);
    if (!IS_CERR_CODE(err, CERR_INVALID_ARGUMENTS))             return EXIT_FAILURE;
    if (synth_get_config(drone)->gain != 0.0f)                  return EXIT_FAILURE;

    synth_oscillator_config osc = synth_get_config(drone)->oscillators[0];
    osc.waveform = SYNTH_WAVE_SINE;
    err = synth_set_oscillator(drone, 0, &osc);
    if (!IS_CERR_CODE(err, CERR_INVALID_OPERATION))             return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "synth helpers", .test = helpers_test },
    { .name = "synth config", .test = config_test },
    { .name = "synth block size", .test = block_size_test },
    { .name = "synth lifecycle", .test = lifecycle_test },
    { .name = "synth live patch", .test = live_patch_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
