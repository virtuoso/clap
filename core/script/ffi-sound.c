// SPDX-License-Identifier: Apache-2.0
//
// FFI over the subtractive synth (sound.h). A script assembles a patch and a
// note list on a Synth handle, then start() creates the engine's synth node
// from them; each start() rebuilds the node, so patch edits take effect on the
// next start. The node pulls its notes on the audio thread, where the
// interpreter cannot be called, so the sequence crosses the boundary as data:
// start() snapshots the note list and hands it to the synth as its built-in
// sequence. An EffectChain handle wraps a sound_effect_chain a synth can be
// routed through. Synths and chains are this module's private per-script state
// (CLAP_SCRIPT_PRIV), dropped with the script, so a hot-reload silences what
// the previous version played.

#include <string.h>

#include "clap.h"
#include "logger.h"
#include "memory.h"
#include "script/ffi.h"
#include "script/internal.h"
#include "sound.h"

/**
 * struct script_synth - a script's synth: patch, notes and the live node
 * @entry:      link to script_priv::synths
 * @config:     patch under construction
 * @notes:      notes under construction
 * @sequence:   snapshot of @notes the synth plays; only touched from the
 *              main thread while @synth is NULL
 * @synth:      engine node, created by start()
 * @bpm:        tempo add_note()/add_rest() convert note values with
 * @tuning:     A4 frequency for add_note(); 0.0 is concert pitch
 * @loop:       whether the sequence wraps around
 * @chain:      chain the node is routed through, applied by start() too
 */
struct script_synth {
    struct list                 entry;
    synth_config                config;
    darray(synth_note, notes);
    darray(synth_note, sequence);
    synth                       *synth;
    struct script_effect_chain  *chain;
    float                       bpm;
    float                       tuning;
    bool                        loop;
};

/**
 * struct script_effect_chain - a script's effect chain
 * @entry:  link to script_priv::effect_chains
 * @chain:  the engine's chain; effects added to it are owned by it
 */
struct script_effect_chain {
    struct list         entry;
    sound_effect_chain  *chain;
};

CLAP_SCRIPT_PRIV(struct list synths)
CLAP_SCRIPT_PRIV(struct list effect_chains)

CLAP_SCRIPT_TYPE("type Synth* = struct { _h: uint }")
CLAP_SCRIPT_TYPE("type EffectChain* = struct { _h: uint }")

static const struct {
    const char      *name;
    synth_waveform  waveform;
} synth_waveform_names[] = {
    { "sine",           SYNTH_WAVE_SINE },
    { "square",         SYNTH_WAVE_SQUARE },
    { "triangle",       SYNTH_WAVE_TRIANGLE },
    { "sawtooth",       SYNTH_WAVE_SAWTOOTH },
    { "white_noise",    SYNTH_WAVE_WHITE_NOISE },
    { "pink_noise",     SYNTH_WAVE_PINK_NOISE },
};

/* The synth a handle refers to; nullptr unless this script holds it, so a delete()d one doesn't dangle */
static struct script_synth *ffi_synth_get(UmkaStackSlot *params, int idx, UmkaStackSlot *result)
{
    struct script_synth *handle = ffi_ptr(params, idx);
    script *s = ffi_script(result);

    struct script_synth *ss;
    list_for_each_entry(ss, &s->priv.synths, entry)
        if (ss == handle)   return ss;

    return nullptr;
}

/* The chain a handle refers to, or nullptr, like ffi_synth_get() */
static struct script_effect_chain *ffi_effect_chain_get(UmkaStackSlot *params, int idx, UmkaStackSlot *result)
{
    struct script_effect_chain *handle = ffi_ptr(params, idx);
    script *s = ffi_script(result);

    struct script_effect_chain *sec;
    list_for_each_entry(sec, &s->priv.effect_chains, entry)
        if (sec == handle)  return sec;

    return nullptr;
}

/* Drop the node first: its teardown waits out the audio thread's read */
static void script_synth_silence(struct script_synth *ss)
{
    if (!ss->synth)     return;

    ref_put(ss->synth);
    ss->synth = nullptr;
}

static void script_synth_delete(struct script_synth *ss)
{
    script_synth_silence(ss);
    darray_clearout(ss->sequence);
    darray_clearout(ss->notes);
    list_del(&ss->entry);
    mem_free(ss);
}

/* A new, empty synth: everything comes from the script, nothing is defaulted */
CLAP_SCRIPT_FN(ffi_synth, "fn synth*(): Synth")
void ffi_synth(UmkaStackSlot *params, UmkaStackSlot *result)
{
    script *s = ffi_script(result);
    struct script_synth *ss = mem_alloc(sizeof(*ss), .zero = 1);

    if (ss) {
        darray_init(ss->notes);
        darray_init(ss->sequence);
        list_append(&s->priv.synths, &ss->entry);
    }

    ffi_ret_ptr(params, result, ss);
}

CLAP_SCRIPT_METHOD(ffi_synth_valid, "fn (s: Synth) valid*(): bool")
void ffi_synth_valid(UmkaStackSlot *params, UmkaStackSlot *result)
{
    ffi_ret_bool(params, result, ffi_synth_get(params, 0, result) != nullptr);
}

/*
 * Waveform is one of "sine", "square", "triangle", "sawtooth", "white_noise",
 * "pink_noise"; frequency is the base pitch in Hz used when the synth has no
 * notes, detune is in cents; neither applies to the noises.
 */
CLAP_SCRIPT_METHOD(ffi_synth_add_oscillator,
                   "fn (s: Synth) add_oscillator*(waveform: str, frequency, detune, gain: real): bool")
void ffi_synth_add_oscillator(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    const char *waveform = ffi_str(params, 1);

    if (!ss || !waveform || ss->config.nr_oscillators >= SYNTH_MAX_OSCILLATORS)
        return ffi_ret_bool(params, result, false);

    for (size_t i = 0; i < array_size(synth_waveform_names); i++) {
        if (strcmp(synth_waveform_names[i].name, waveform)) continue;

        synth_oscillator_config *osc = &ss->config.oscillators[ss->config.nr_oscillators++];
        osc->waveform = synth_waveform_names[i].waveform;
        osc->frequency = ffi_real(params, 2);
        osc->detune = ffi_real(params, 3);
        osc->gain = ffi_real(params, 4);

        return ffi_ret_bool(params, result, true);
    }

    err("unknown waveform '%s'\n", waveform);
    ffi_ret_bool(params, result, false);
}

/* Lowpass cutoff in Hz, resonance as Q (0.707 is flat), envelope amount in octaves */
CLAP_SCRIPT_METHOD(ffi_synth_set_filter, "fn (s: Synth) set_filter*(cutoff, resonance, env_amount: real)")
void ffi_synth_set_filter(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    if (!ss)    return;

    ss->config.filter.cutoff = ffi_real(params, 1);
    ss->config.filter.resonance = ffi_real(params, 2);
    ss->config.filter.env_amount = ffi_real(params, 3);
}

static void ffi_synth_envelope(UmkaStackSlot *params, synth_envelope_config *env)
{
    env->attack_ms = ffi_real(params, 1);
    env->decay_ms = ffi_real(params, 2);
    env->sustain = ffi_real(params, 3);
    env->release_ms = ffi_real(params, 4);
}

CLAP_SCRIPT_METHOD(ffi_synth_set_amp_env,
                   "fn (s: Synth) set_amp_env*(attack_ms, decay_ms, sustain, release_ms: real)")
void ffi_synth_set_amp_env(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    if (ss)     ffi_synth_envelope(params, &ss->config.amp_env);
}

CLAP_SCRIPT_METHOD(ffi_synth_set_filter_env,
                   "fn (s: Synth) set_filter_env*(attack_ms, decay_ms, sustain, release_ms: real)")
void ffi_synth_set_filter_env(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    if (ss)     ffi_synth_envelope(params, &ss->config.filter_env);
}

CLAP_SCRIPT_METHOD(ffi_synth_set_gain, "fn (s: Synth) set_gain*(gain: real)")
void ffi_synth_set_gain(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    if (ss)     ss->config.gain = ffi_real(params, 1);
}

static const struct {
    const char      *name;
    sound_category  category;
} sound_category_names[] = {
    { "none",   SOUND_CAT_NONE },
    { "music",  SOUND_CAT_MUSIC },
    { "sfx",    SOUND_CAT_SFX },
};

/* Master volume the synth follows: "music", "sfx", or "none" for neither */
CLAP_SCRIPT_METHOD(ffi_synth_set_category, "fn (s: Synth) set_category*(category: str): bool")
void ffi_synth_set_category(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    const char *name = ffi_str(params, 1);
    if (!ss || !name)   return ffi_ret_bool(params, result, false);

    for (size_t i = 0; i < array_size(sound_category_names); i++) {
        if (strcmp(sound_category_names[i].name, name)) continue;

        ss->config.category = sound_category_names[i].category;

        return ffi_ret_bool(params, result, true);
    }

    err("unknown category '%s'\n", name);
    ffi_ret_bool(params, result, false);
}

/* Fraction of each note held before the envelopes release, (0, 1]; needed with notes */
CLAP_SCRIPT_METHOD(ffi_synth_set_gate, "fn (s: Synth) set_gate*(gate: real)")
void ffi_synth_set_gate(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    if (ss)     ss->config.gate = ffi_real(params, 1);
}

/* Tempo in quarter notes per minute, for add_note()/add_rest() */
CLAP_SCRIPT_METHOD(ffi_synth_set_tempo, "fn (s: Synth) set_tempo*(bpm: real)")
void ffi_synth_set_tempo(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    if (ss)     ss->bpm = ffi_real(params, 1);
}

/* A4 in Hz for add_note(); 0.0 is concert pitch, 440 Hz */
CLAP_SCRIPT_METHOD(ffi_synth_set_tuning, "fn (s: Synth) set_tuning*(a4_hz: real)")
void ffi_synth_set_tuning(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    if (ss)     ss->tuning = ffi_real(params, 1);
}

CLAP_SCRIPT_METHOD(ffi_synth_set_loop, "fn (s: Synth) set_loop*(loop: bool)")
void ffi_synth_set_loop(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    if (ss)     ss->loop = ffi_bool(params, 1);
}

static bool script_synth_add(struct script_synth *ss, float frequency, float duration_ms)
{
    synth_note *note = darray_add(ss->notes);
    if (!note)  return false;

    note->frequency = frequency;
    note->duration_ms = duration_ms;

    return true;
}

/* A note by pitch name ("A4", "Bb3", "C#5") and value (0.25 is a quarter) at the synth's tempo */
CLAP_SCRIPT_METHOD(ffi_synth_add_note, "fn (s: Synth) add_note*(pitch: str, value: real): bool")
void ffi_synth_add_note(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    const char *pitch = ffi_str(params, 1);
    if (!ss || !pitch)  return ffi_ret_bool(params, result, false);

    float frequency = CRES_RET(synth_pitch_frequency(pitch, ss->tuning), {
        err_cerr(__resp, "bad pitch '%s'\n", pitch);
        return ffi_ret_bool(params, result, false);
    });
    float duration = CRES_RET(synth_note_duration_ms(ss->bpm, ffi_real(params, 2)), {
        err_cerr(__resp, "bad note value %f at %f bpm\n", ffi_real(params, 2), ss->bpm);
        return ffi_ret_bool(params, result, false);
    });

    ffi_ret_bool(params, result, script_synth_add(ss, frequency, duration));
}

/* Silence for a note value at the synth's tempo */
CLAP_SCRIPT_METHOD(ffi_synth_add_rest, "fn (s: Synth) add_rest*(value: real): bool")
void ffi_synth_add_rest(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    if (!ss)    return ffi_ret_bool(params, result, false);

    float duration = CRES_RET(synth_note_duration_ms(ss->bpm, ffi_real(params, 1)), {
        err_cerr(__resp, "bad rest value %f at %f bpm\n", ffi_real(params, 1), ss->bpm);
        return ffi_ret_bool(params, result, false);
    });

    ffi_ret_bool(params, result, script_synth_add(ss, 0.0f, duration));
}

/* A note in the synth's native units: frequency in Hz (0.0 is a rest), duration in ms */
CLAP_SCRIPT_METHOD(ffi_synth_add_tone, "fn (s: Synth) add_tone*(frequency, duration_ms: real): bool")
void ffi_synth_add_tone(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    if (!ss)    return ffi_ret_bool(params, result, false);

    ffi_ret_bool(params, result, script_synth_add(ss, ffi_real(params, 1), ffi_real(params, 2)));
}

/* Forget the notes; the running snapshot, if any, plays on until stop() or start() */
CLAP_SCRIPT_METHOD(ffi_synth_clear_notes, "fn (s: Synth) clear_notes*()")
void ffi_synth_clear_notes(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    if (ss)     darray_clearout(ss->notes);
}

/*
 * (Re)create the node from the patch and the notes and start it. Without
 * notes the oscillators play their base frequencies until stop(); with notes
 * the snapshot is played from the top, once or looped. Returns false, with the
 * reason logged, when the patch is rejected or there is no sound.
 */
CLAP_SCRIPT_METHOD(ffi_synth_start, "fn (s: Synth) start*(): bool")
void ffi_synth_start(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct clap_context *ctx = ffi_ctx(result);
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    if (!ss)    return ffi_ret_bool(params, result, false);

    sound_context *sound = clap_get_sound(ctx);
    if (!sound) {
        err("no sound context to play a synth on\n");
        return ffi_ret_bool(params, result, false);
    }

    script_synth_silence(ss);

    size_t nr = darray_count(ss->notes);
    darray_clearout(ss->sequence);
    if (nr) {
        if (!darray_resize(ss->sequence, nr)) {
            err("cannot snapshot %zu notes\n", nr);
            return ffi_ret_bool(params, result, false);
        }
        memcpy(ss->sequence.x, ss->notes.x, nr * sizeof(synth_note));
    }
    ss->config.notes = nr ? ss->sequence.x : nullptr;
    ss->config.nr_notes = nr;
    ss->config.loop = ss->loop;
    ss->config.next_note = nullptr;
    ss->config.next_note_data = nullptr;

    ss->synth = CRES_RET(ref_new_checked(synth, .ctx = sound, .config = &ss->config), {
        err_cerr(__resp, "cannot create synth\n");
        return ffi_ret_bool(params, result, false);
    });
    if (ss->chain)  synth_set_effect_chain(ss->synth, ss->chain->chain);
    synth_start(ss->synth);

    ffi_ret_bool(params, result, true);
}

/* Route the output through a chain, or straight to the output for an invalid handle */
CLAP_SCRIPT_METHOD(ffi_synth_set_effect_chain, "fn (s: Synth) set_effect_chain*(c: EffectChain)")
void ffi_synth_set_effect_chain(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    if (!ss)    return;

    ss->chain = ffi_effect_chain_get(params, 1, result);
    if (ss->synth)  synth_set_effect_chain(ss->synth, ss->chain ? ss->chain->chain : nullptr);
}

/* Silence immediately; start() resumes the sequence from the top */
CLAP_SCRIPT_METHOD(ffi_synth_stop, "fn (s: Synth) stop*()")
void ffi_synth_stop(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    if (ss && ss->synth)    synth_stop(ss->synth);
}

CLAP_SCRIPT_METHOD(ffi_synth_delete, "fn (s: Synth) delete*()")
void ffi_synth_delete(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_synth *ss = ffi_synth_get(params, 0, result);
    if (ss)     script_synth_delete(ss);
}

/*
 * Effect chains: reverb and delay processors a synth's output is sent through.
 * Effects are appended in call order and are owned by the chain.
 */

static void script_effect_chain_delete(script *s, struct script_effect_chain *sec)
{
    /* the engine detaches live synths when the chain goes; drop the script's routing too */
    struct script_synth *ss;
    list_for_each_entry(ss, &s->priv.synths, entry)
        if (ss->chain == sec)   ss->chain = nullptr;

    ref_put(sec->chain);
    list_del(&sec->entry);
    mem_free(sec);
}

/* An empty, enabled chain feeding the output; invalid without sound, with the reason logged */
CLAP_SCRIPT_FN(ffi_effect_chain, "fn effect_chain*(): EffectChain")
void ffi_effect_chain(UmkaStackSlot *params, UmkaStackSlot *result)
{
    script *s = ffi_script(result);
    sound_context *sound = clap_get_sound(script_context(s));

    if (!sound) {
        err("no sound context to build an effect chain on\n");
        return ffi_ret_ptr(params, result, nullptr);
    }

    LOCAL_SET(sound_effect_chain, chain) = CRES_RET(ref_new_checked(sound_effect_chain, .ctx = sound), {
        err_cerr(__resp, "cannot create effect chain\n");
        return ffi_ret_ptr(params, result, nullptr);
    });

    struct script_effect_chain *sec = mem_alloc(sizeof(*sec), .zero = 1);
    if (!sec)   return ffi_ret_ptr(params, result, nullptr);

    sec->chain = NOCU(chain);
    list_append(&s->priv.effect_chains, &sec->entry);

    ffi_ret_ptr(params, result, sec);
}

CLAP_SCRIPT_METHOD(ffi_effect_chain_valid, "fn (c: EffectChain) valid*(): bool")
void ffi_effect_chain_valid(UmkaStackSlot *params, UmkaStackSlot *result)
{
    ffi_ret_bool(params, result, ffi_effect_chain_get(params, 0, result) != nullptr);
}

static const struct {
    const char  *name;
    reverb_type type;
} reverb_names[] = {
    { "small_room", REVERB_SMALL_ROOM },
    { "hall",       REVERB_HALL },
};

/* Append a Schroeder reverb: room is "small_room" or "hall"; size, damping and wet mix are [0, 1] */
CLAP_SCRIPT_METHOD(ffi_effect_chain_add_reverb,
                   "fn (c: EffectChain) add_reverb*(room: str, room_size, damping, wet: real): bool")
void ffi_effect_chain_add_reverb(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_effect_chain *sec = ffi_effect_chain_get(params, 0, result);
    const char *room = ffi_str(params, 1);
    if (!sec || !room)  return ffi_ret_bool(params, result, false);

    for (size_t i = 0; i < array_size(reverb_names); i++) {
        if (strcmp(reverb_names[i].name, room))     continue;

        sound_context *sound = clap_get_sound(ffi_ctx(result));
        LOCAL_SET(sound_effect, effect) = CRES_RET(
            ref_new_checked(sound_effect, .ctx = sound, .type = SOUND_EFFECT_REVERB,
                            .reverb_type = reverb_names[i].type, .room_size = ffi_real(params, 2),
                            .damping = ffi_real(params, 3), .wet_dry = ffi_real(params, 4)),
            {
                err_cerr(__resp, "cannot create reverb\n");
                return ffi_ret_bool(params, result, false);
            });
        sound_effect_chain_add(sec->chain, effect);

        return ffi_ret_bool(params, result, true);
    }

    err("unknown reverb '%s'\n", room);
    ffi_ret_bool(params, result, false);
}

/* Append a stereo delay: per-channel delay in ms, feedback and wet mix in [0, 1] */
CLAP_SCRIPT_METHOD(ffi_effect_chain_add_delay,
                   "fn (c: EffectChain) add_delay*(left_ms, right_ms, feedback, wet: real): bool")
void ffi_effect_chain_add_delay(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_effect_chain *sec = ffi_effect_chain_get(params, 0, result);
    if (!sec)   return ffi_ret_bool(params, result, false);

    sound_context *sound = clap_get_sound(ffi_ctx(result));
    LOCAL_SET(sound_effect, effect) = CRES_RET(
        ref_new_checked(sound_effect, .ctx = sound, .type = SOUND_EFFECT_DELAY,
                        .delay_ms = { ffi_real(params, 1), ffi_real(params, 2) },
                        .feedback = ffi_real(params, 3), .wet_dry = ffi_real(params, 4)),
        {
            err_cerr(__resp, "cannot create delay\n");
            return ffi_ret_bool(params, result, false);
        });
    sound_effect_chain_add(sec->chain, effect);

    ffi_ret_bool(params, result, true);
}

/* A disabled chain passes its input through untouched */
CLAP_SCRIPT_METHOD(ffi_effect_chain_set_enabled, "fn (c: EffectChain) set_enabled*(enabled: bool)")
void ffi_effect_chain_set_enabled(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_effect_chain *sec = ffi_effect_chain_get(params, 0, result);
    if (sec)    sound_effect_chain_enable(sec->chain, ffi_bool(params, 1));
}

/* Drop the chain; synths routed through it go back to the output */
CLAP_SCRIPT_METHOD(ffi_effect_chain_delete, "fn (c: EffectChain) delete*()")
void ffi_effect_chain_delete(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct script_effect_chain *sec = ffi_effect_chain_get(params, 0, result);
    if (sec)    script_effect_chain_delete(ffi_script(result), sec);
}

/* The helpers stand alone too, for scripts that time or tune their own way */

/* Frequency of a named pitch; tuning 0.0 is A4 = 440 Hz. 0.0 and a logged error on a bad name. */
CLAP_SCRIPT_FN(ffi_pitch_frequency, "fn pitch_frequency*(pitch: str, tuning: real): real")
void ffi_pitch_frequency(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const char *pitch = ffi_str(params, 0);
    float tuning = ffi_real(params, 1);

    float frequency = CRES_RET(synth_pitch_frequency(pitch, tuning), {
        err_cerr(__resp, "bad pitch '%s'\n", pitch ? pitch : "");
        return ffi_ret_real(params, result, 0.0);
    });

    ffi_ret_real(params, result, frequency);
}

/* Milliseconds of a note value (0.25 is a quarter) at a tempo. 0.0 and a logged error if invalid. */
CLAP_SCRIPT_FN(ffi_note_duration_ms, "fn note_duration_ms*(bpm, value: real): real")
void ffi_note_duration_ms(UmkaStackSlot *params, UmkaStackSlot *result)
{
    float bpm = ffi_real(params, 0);
    double value = ffi_real(params, 1);

    float duration = CRES_RET(synth_note_duration_ms(bpm, value), {
        err_cerr(__resp, "bad note value %f at %f bpm\n", value, bpm);
        return ffi_ret_real(params, result, 0.0);
    });

    ffi_ret_real(params, result, duration);
}

CLAP_SCRIPT_DONE(script_synths_done)
void script_synths_done(script *s)
{
    struct script_synth *ss, *iter;
    list_for_each_entry_iter(ss, iter, &s->priv.synths, entry)
        script_synth_delete(ss);

    struct script_effect_chain *sec, *itc;
    list_for_each_entry_iter(sec, itc, &s->priv.effect_chains, entry)
        script_effect_chain_delete(s, sec);
}
