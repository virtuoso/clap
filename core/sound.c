// SPDX-License-Identifier: Apache-2.0
#include "messagebus.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "miniaudio.h"

#include "object.h"
#include "clap.h"
#include "librarian.h"
#include "logger.h"
#include "sound.h"

typedef struct sound_context {
    ma_engine   engine;
    bool        started;
    /* what clap_cpus_reserve() granted of SOUND_NR_THREADS */
    unsigned int nr_threads;
    struct list sounds;
    struct list chains;
    struct list synths;
    clap_context *clap_ctx;
    float       master_music;
    float       master_sfx;
} sound_context;

typedef struct sound {
    char                *name;
    sound_context       *ctx;
    ma_sound            sound;
    ma_audio_buffer     buffer;
    float               *pcm;
    unsigned int        nr_channels;
    float               gain;
    sound_category      category;
    bool                looping;
    bool                deferred_play;
    sound_effect_chain  *effect_chain;
    struct list         entry;
    struct list         chain_entry;
    struct ref          ref;
} sound;

/**
 * struct sound_effect_chain - sound effect chain
 * @node:       miniaudio's node
 * @effects:    list of effects in the chain
 * @sounds:     list of sounds sending to the chain
 * @enabled:    chain enabled/disabled
 * @ref:        lifetime management
 * @entry:      link to sound_context::chains
 */
struct sound_effect_chain {
    ma_node_base    node;
    struct list     effects;
    struct list     sounds;
    _Atomic bool    enabled;
    struct ref      ref;
    struct list     entry;
};

typedef enum synth_env_stage {
    SYNTH_ENV_IDLE = 0,
    SYNTH_ENV_ATTACK,
    SYNTH_ENV_DECAY,
    SYNTH_ENV_SUSTAIN,
    SYNTH_ENV_RELEASE,
} synth_env_stage;

/**
 * struct synth_envelope - ADSR envelope state
 * @config:         timings and sustain level
 * @level:          current output, [0.0, 1.0]
 * @step:           per-frame level change in the current stage
 * @stage:          current stage
 * @sample_rate:    frames per second, turns the config's ms into steps
 */
typedef struct synth_envelope {
    synth_envelope_config   config;
    float                   level;
    float                   step;
    synth_env_stage         stage;
    unsigned int            sample_rate;
} synth_envelope;

/**
 * struct synth - subtractive synth node
 * @node:           miniaudio node: a source with one output bus; must be first
 * @ctx:            sound context
 * @config:         patch, copied at creation
 * @osc:            oscillators
 * @filter:         resonant lowpass
 * @amp_env:        amplitude envelope
 * @filter_env:     cutoff envelope
 * @sample_rate:    engine sample rate
 * @channels:       engine channel count
 * @note_frames:    frames left in the current note; 0 asks for the next one
 * @gate_frames:    frames left until the envelopes release; 0 once released
 * @frames:         frames rendered so far, aligns chunks independently of the
 *                  host's block size
 * @ref:            lifetime management
 * @entry:          link to sound_context::synths
 * @pos:            next entry of synth_config::notes
 * @sequence_done:  the sequence is over: silence from here on
 */
struct synth {
    ma_node_base    node;
    sound_context   *ctx;
    synth_config    config;
    ma_waveform     osc[SYNTH_MAX_OSCILLATORS];
    ma_lpf2         filter;
    synth_envelope  amp_env;
    synth_envelope  filter_env;
    unsigned int    sample_rate;
    unsigned int    channels;
    ma_uint64       note_frames;
    ma_uint64       gate_frames;
    ma_uint64       frames;
    struct ref      ref;
    struct list     entry;
    unsigned int    pos;
    bool            sequence_done;
};

 typedef struct ov_cb_data {
    void    *buf;
    size_t  size;
    off_t   off;
} ov_cb_data;

static float sound_master_for(sound_context *ctx, sound_category category)
{
    switch (category) {
    case SOUND_CAT_MUSIC:   return ctx->master_music;
    case SOUND_CAT_SFX:     return ctx->master_sfx;
    case SOUND_CAT_NONE:
    default:                return 1.0f;
    }
}

static void sound_apply_gain(sound *sound)
{
    if (!sound->ctx->started)   return;

    float g = sound->gain * sound_master_for(sound->ctx, sound->category);
    ma_sound_set_min_gain(&sound->sound, g);
    ma_sound_set_max_gain(&sound->sound, g);
}

void sound_set_gain(sound *sound, float gain)
{
    sound->gain = gain;
    sound_apply_gain(sound);
}

float sound_get_gain(sound *sound)
{
    return sound->gain;
}

void sound_set_master_volume(sound_context *ctx, sound_category category, float volume)
{
    if (volume < 0.0f)  volume = 0.0f;
    if (volume > 1.0f)  volume = 1.0f;

    switch (category) {
    case SOUND_CAT_MUSIC:   ctx->master_music = volume; break;
    case SOUND_CAT_SFX:     ctx->master_sfx   = volume; break;
    default:                return;
    }

    sound *s;
    list_for_each_entry(s, &ctx->sounds, entry)
        if (s->category == category)  sound_apply_gain(s);
}

float sound_get_master_volume(sound_context *ctx, sound_category category)
{
    return sound_master_for(ctx, category);
}

static cerr do_sound_make(sound *sound, sound_context *ctx)
{
    ov_cb_data cb_data = {};
    LOCAL_SET(lib_handle, lh) = lib_read_file(RES_ASSET, sound->name, &cb_data.buf, &cb_data.size);
    if (!lh || lh->state == RES_ERROR)
        return CERR_SOUND_NOT_LOADED;

    auto decoder_config = ma_decoder_config_init(
        ma_format_f32,
        ma_engine_get_channels(&ctx->engine),
        ma_engine_get_sample_rate(&ctx->engine)
    );

    ma_decoder decoder;
    auto result = ma_decoder_init_memory(cb_data.buf, cb_data.size, &decoder_config, &decoder);
    if (result != MA_SUCCESS)   return CERR_SOUND_NOT_LOADED;

    ma_uint64 nr_frames;
    result = ma_decoder_get_length_in_pcm_frames(&decoder, &nr_frames);
    if (result != MA_SUCCESS)   goto out_decoder;

    sound->pcm = mem_alloc(sizeof(float), .nr = nr_frames * decoder.outputChannels);
    if (!sound->pcm)            goto out_decoder;

    ma_uint64 nr_frames_decoded;
    result = ma_decoder_read_pcm_frames(&decoder, sound->pcm, nr_frames, &nr_frames_decoded);
    if (result != MA_SUCCESS)   goto out_pcm;

    ma_audio_buffer_config buffer_config = ma_audio_buffer_config_init(decoder.outputFormat, decoder.outputChannels, nr_frames_decoded, sound->pcm, NULL);
    result = ma_audio_buffer_init(&buffer_config, &sound->buffer);
    if (result != MA_SUCCESS)   goto out_pcm;

    ma_decoder_uninit(&decoder);

    result = ma_sound_init_from_data_source(
        &ctx->engine,
        &sound->buffer,
        MA_SOUND_FLAG_WAIT_INIT,
        NULL,
        &sound->sound
    );
    if (result != MA_SUCCESS) {
        ma_audio_buffer_uninit(&sound->buffer);
        mem_free(sound->pcm);
        return CERR_SOUND_NOT_LOADED;
    }

    return CERR_OK;

out_pcm:
    mem_free(sound->pcm);
    sound->pcm = NULL;

out_decoder:
    ma_decoder_uninit(&decoder);

    return CERR_SOUND_NOT_LOADED;
}

static cerr sound_make(struct ref *ref, void *_opts)
{
    rc_init_opts(sound) *opts = _opts;
    if (!opts->name || !opts->ctx)
        return CERR_INVALID_ARGUMENTS;

    /*
     * XXX: check that the file even exists
     */

    sound *sound = container_of(ref, struct sound, ref);

    sound->ctx = opts->ctx;
    sound->name = strdup(opts->name);
    sound->category = opts->category;
    sound->gain = 1.0f;

    if (opts->ctx->started)
        CERR_RET(do_sound_make(sound, opts->ctx), { free(sound->name); return __cerr; });

    list_append(&opts->ctx->sounds, &sound->entry);

    return CERR_OK;
}

static void sound_drop(struct ref *ref)
{
    sound *sound = container_of(ref, struct sound, ref);

    if (sound->effect_chain)
        sound_set_effect_chain(sound, NULL);

    ma_audio_buffer_uninit(&sound->buffer);
    ma_sound_uninit(&sound->sound);
    mem_free(sound->pcm);

    list_del(&sound->entry);
    free(sound->name);
}

DEFINE_REFCLASS2(sound);

DEFINE_CLEANUP(sound, if (*p) ref_put(*p))

void sound_set_looping(sound *sound, bool looping)
{
    sound->looping = looping;

    if (!sound->ctx->started)   return;

    ma_sound_set_looping(&sound->sound, looping);
}

void sound_play(sound *sound)
{
    if (!sound->ctx->started) {
        sound->deferred_play = true;
        return;
    }

    sound->deferred_play = false;
    if (ma_sound_is_playing(&sound->sound))
        ma_sound_seek_to_pcm_frame(&sound->sound, 0);

    ma_sound_start(&sound->sound);
}

static void do_sound_init(sound_context *ctx)
{
    auto config = ma_engine_config_init();
    config.noAutoStart = true;

    auto result = ma_engine_init(&config, &ctx->engine);
    if (result != MA_SUCCESS)   return;

    sound *s;
    list_for_each_entry(s, &ctx->sounds, entry) {
        CERR_RET(do_sound_make(s, ctx), continue);
        ma_sound_set_looping(&s->sound, s->looping);
    }

    result = ma_engine_start(&ctx->engine);
    if (result != MA_SUCCESS)   goto err_engine;

    ctx->started = true;

    /*
     * Now that ->started is true, sound_apply_gain() will actually reach
     * the ma_sound_set_*_gain calls.
     */
    list_for_each_entry(s, &ctx->sounds, entry)
        sound_apply_gain(s);

    list_for_each_entry(s, &ctx->sounds, entry)
        if (s->deferred_play) sound_play(s);

    message_send(
        ctx->clap_ctx,
        &(struct message) {
            .type   = MT_COMMAND,
            .cmd    = { .sound_ready = 1 },
        }
    );

    return;

err_engine:
    ma_engine_uninit(&ctx->engine);
}

#ifdef CONFIG_BROWSER
static int sound_handle_input(struct clap_context *clap_ctx, struct message *m, void *data)
{
    sound_context *ctx = data;

    if (ctx->started)   return MSG_HANDLED;

    if (m->input.mouse_click /*|| m->input.mouse_move */|| m->input.keyboard)
        do_sound_init(ctx);

    return MSG_HANDLED;
}
#endif /* CONFIG_BROWSER */

DEFINE_CLEANUP(sound_context, if (*p) mem_free(*p))

/*
 * What miniaudio runs on threads of its own: the resource manager's job
 * thread everywhere, and the device's data loop where the backend polls
 * rather than calls back (PulseAudio and ALSA do; CoreAudio and the audio
 * worklet don't)
 */
#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
/** SOUND_NR_THREADS - threads miniaudio runs: the job thread */
#define SOUND_NR_THREADS 1
#else
/** SOUND_NR_THREADS - threads miniaudio runs: the job thread and the data loop */
#define SOUND_NR_THREADS 2
#endif /* !(__EMSCRIPTEN__ || __APPLE__) */

cresp(sound_context) sound_init(clap_context *clap_ctx)
{
    LOCAL_SET(sound_context, ctx) = mem_alloc(sizeof(*ctx), .zero = 1);
    if (!ctx)   return cresp_error(sound_context, CERR_NOMEM);

    ctx->clap_ctx = clap_ctx;
    ctx->master_music = 1.0f;
    ctx->master_sfx   = 1.0f;
    list_init(&ctx->sounds);
    list_init(&ctx->chains);
    list_init(&ctx->synths);

    ctx->nr_threads = clap_cpus_reserve(clap_ctx, SOUND_NR_THREADS);

#ifdef CONFIG_BROWSER
    subscribe(ctx->clap_ctx, MT_INPUT, sound_handle_input, ctx);
#else
    do_sound_init(ctx);
#endif /* !CONFIG_BROWSER */

    return cresp_val(sound_context, NOCU(ctx));
}

void sound_done(sound_context *ctx)
{
    sound *sound, *its;

    list_for_each_entry_iter(sound, its, &ctx->sounds, entry) {
        ref_put(sound);
    }

    sound_effect_chain *chain, *itc;

    list_for_each_entry_iter(chain, itc, &ctx->chains, entry)
        ref_put(chain);

    synth *synth, *itsy;

    list_for_each_entry_iter(synth, itsy, &ctx->synths, entry)
        ref_put(synth);

    if (ctx->started) {
        ma_engine_stop(&ctx->engine);
        ma_engine_uninit(&ctx->engine);
    }

    /* a sound_init_headless() context has no clap_context and reserved no CPUs */
    if (ctx->clap_ctx)  clap_cpus_release(ctx->clap_ctx, ctx->nr_threads);
    mem_free(ctx);
}

/****************************************************************************
 * Sound effects (audio post processing)
 ****************************************************************************/

typedef cerr (*sound_effect_init_fn)(sound_effect *effect, rc_init_opts(sound_effect) *opts);
typedef void (*sound_effect_done_fn)(sound_effect *effect);
typedef void (*sound_effect_process_fn)(sound_effect *effect, float *buffer,
                                        unsigned int frames, unsigned int channels);

/**
 * struct sound_effect_desc - sound effect descriptor
 * @name:       effect name (e.g. "reverb")
 * @init:       effect instance initializer
 * @done:       effect instance destructor
 * @process:    signal processing function
 */
typedef struct sound_effect_desc {
    const char              *name;
    sound_effect_init_fn    init;
    sound_effect_done_fn    done;
    sound_effect_process_fn process;
} sound_effect_desc;

/**
 * struct sound_effect - sound effect instance
 * @desc:   pointer to effect descriptor
 * @data:   effect's private data
 * @entry:  entry to effect chain's list
 * @ref:    lifetime management
 */
struct sound_effect {
    const sound_effect_desc *desc;
    void                    *data;
    struct list             entry;
    struct ref              ref;
};

/****************************************************************************
 * Sound effects: reverb
 ****************************************************************************/

static const struct {
    size_t  nr_combs;
    size_t  *comb_sizes;
    size_t  *allpass_sizes;
} reverb_types[] = {
    [REVERB_SMALL_ROOM] = {
        .nr_combs       = 4,
        .comb_sizes     = (size_t[]){ 1200, 1433, 1597, 1759 },
        .allpass_sizes  = (size_t[]){ 149, 211 },
    },
    [REVERB_HALL] = {
        .nr_combs       = 6,
        .comb_sizes     = (size_t[]){ 1723, 1999, 2239, 2503, 2801, 3203 },
        .allpass_sizes  = (size_t[]){ 173, 263 }
    }
};

/**
 * struct reverb_comb - comb filter for reverb
 * @buffer:         circular delay buffer; size = max hall comb pass size
 * @size:           buffer size in samples
 * @pos:            current position in buffer
 * @feedback:       feedback amount (controls decay time)
 * @filterstore:    last output for damping filter
 * @damp1:          damping coefficient
 * @damp2:          damping coefficient (1 - damp1)
 */
typedef struct reverb_comb {
    float   buffer[3203];
    int     size;
    int     pos;
    float   feedback;
    float   filterstore;
    float   damp1;
    float   damp2;
} reverb_comb;

/**
 * struct reverb_allpass - allpass filter for reverb
 * @buffer: circular delay buffer
 * @size:   buffer size in samples
 * @pos:    current position in buffer
 */
typedef struct reverb_allpass {
    float   buffer[556];
    int     size;
    int     pos;
} reverb_allpass;

/**
 * struct reverb_data - reverb effect state
 * @comb:           array of comb filters (creates early reflections)
 * @allpass:        array of allpass filters (diffuses reflections)
 * @wet:            wet signal level (0.0-1.0)
 * @dry:            dry signal level (0.0-1.0)
 * @sample_rate:    audio sample rate
 */
typedef struct reverb_data {
    reverb_comb     comb[6];
    reverb_allpass  allpass[2];
    float           wet;
    float           dry;
    unsigned int    sample_rate;
    reverb_type     type;
} reverb_data;

/*
 * Comb filter: delays input and feeds it back with gain < 1, creating
 * exponentially decaying echoes at intervals of the delay time. Multiple
 * comb filters with different delay times create the dense early reflections
 * characteristic of room reverberation.
 */
static float reverb_comb_process(reverb_comb *comb, float input)
{
    float output = comb->buffer[comb->pos];
    /* One-pole lowpass filter dampens high frequencies in the feedback */
    comb->filterstore = (output * comb->damp2) + (comb->filterstore * comb->damp1);
    comb->buffer[comb->pos] = input + (comb->filterstore * comb->feedback);
    comb->pos = (comb->pos + 1) % comb->size;
    return output;
}

/*
 * Allpass filter: delays input and mixes it with feedback/feedforward to create
 * dense, diffuse reflections without coloring the frequency response. Used after
 * comb filters to smooth out the reverb tail and eliminate metallic artifacts.
 */
static float reverb_allpass_process(reverb_allpass *allpass, float input)
{
    float buffered = allpass->buffer[allpass->pos];
    float output = buffered - input;
    allpass->buffer[allpass->pos] = input + (buffered * 0.5f);
    allpass->pos = (allpass->pos + 1) % allpass->size;
    return output;
}

/*
 * Schroeder reverb algorithm (1962): parallel comb filters summed, then
 * cascaded allpass filters for diffusion. Classic, computationally efficient
 * artificial reverb, though simpler than modern algorithms like FDN or convolution.
 */
static void reverb_process(sound_effect *effect, float *buffer,
                           unsigned int frames, unsigned int channels)
{
    reverb_data *reverb = effect->data;
    auto rtype = &reverb_types[reverb->type];

    for (unsigned int i = 0; i < frames; i++) {
        for (unsigned int ch = 0; ch < channels; ch++) {
            float input = buffer[i * channels + ch];
            float output = 0.0f;

            /* Sum parallel comb filters (early reflections) */
            for (int c = 0; c < rtype->nr_combs; c++)
                output += reverb_comb_process(&reverb->comb[c], input);

            output /= (float)rtype->nr_combs;

            /* Cascade allpass filters (diffusion/smoothing) */
            for (int a = 0; a < array_size(reverb->allpass); a++)
                output = reverb_allpass_process(&reverb->allpass[a], output);

            /* Mix dry and wet signals */
            buffer[i * channels + ch] = input * reverb->dry + output * reverb->wet;
        }
    }
}

/* The room scales every line, and one left without a sample wraps its position modulo 0 */
static bool reverb_room_fits(reverb_type type, float room_size)
{
    auto rtype = &reverb_types[type];

    for (size_t i = 0; i < rtype->nr_combs; i++)
        if ((int)(rtype->comb_sizes[i] * room_size) < 1)    return false;

    for (size_t i = 0; i < array_size(((reverb_data *)nullptr)->allpass); i++)
        if ((int)(rtype->allpass_sizes[i] * room_size) < 1) return false;

    return true;
}

static cerr reverb_init(sound_effect *effect, rc_init_opts(sound_effect) *opts)
{
    if (opts->reverb_type >= array_size(reverb_types))
        return CERR_INVALID_ARGUMENTS;
    /* written to reject NaN, which fails every comparison */
    if (!(opts->room_size > 0.0f && opts->room_size <= 1.0f))   return CERR_INVALID_ARGUMENTS;
    if (!reverb_room_fits(opts->reverb_type, opts->room_size))  return CERR_INVALID_ARGUMENTS;
    if (!(opts->damping >= 0.0f && opts->damping <= 1.0f))      return CERR_INVALID_ARGUMENTS;
    if (!(opts->wet_dry >= 0.0f && opts->wet_dry <= 1.0f))      return CERR_INVALID_ARGUMENTS;

    reverb_data *reverb = mem_alloc(sizeof(*reverb), .zero = 1);
    if (!reverb)    return CERR_NOMEM;

    reverb->type = opts->reverb_type;
    reverb->sample_rate = ma_engine_get_sample_rate(&opts->ctx->engine);
    reverb->wet = opts->wet_dry;
    reverb->dry = 1.0f - opts->wet_dry;

    auto rtype = &reverb_types[reverb->type];

    for (int i = 0; i < rtype->nr_combs; i++) {
        int size = (int)(rtype->comb_sizes[i] * opts->room_size);
        reverb->comb[i].size = size;
        reverb->comb[i].pos = 0;
        reverb->comb[i].feedback = 0.84f; /* XXX: parameterize decay */
        reverb->comb[i].filterstore = 0.0f;
        reverb->comb[i].damp1 = opts->damping;
        reverb->comb[i].damp2 = 1.0f - opts->damping;
    }

    for (int i = 0; i < array_size(reverb->allpass); i++) {
        int size = (int)(rtype->allpass_sizes[i] * opts->room_size);
        reverb->allpass[i].size = size;
        reverb->allpass[i].pos = 0;
    }

    effect->data = reverb;
    return CERR_OK;
}

static void reverb_done(sound_effect *effect)
{
    mem_free(effect->data);
}

/****************************************************************************
 * Sound effects: delay
 ****************************************************************************/

/*
 * We assume a max sample rate of 48kHz and max delay of 2 seconds.
 * This gives a buffer of 2 * 48000 = 96000 samples per channel.
 */
#define DELAY_BUFFER_MAX_SAMPLES 96000

/**
 * struct delay_data - delay effect state
 * @buffer:         delayed sample ring buffer (x2 for stereo)
 * @size:           actual buffer size in samples
 * @write_pos:      write position in the ring buffer
 * @delay_samples:  per-channel delay in samples
 * @feedback:       amount of feedback [0.0f, 1.0f]
 * @wet:            amount of wet signal [0.0f, 1.0f]
 * @dry:            amount of dry signal (1 - wet)
 * @sample_rate:    audio sample rate
 */
typedef struct delay_data {
    float           buffer[DELAY_BUFFER_MAX_SAMPLES * 2];
    unsigned int    size;
    unsigned int    write_pos;
    unsigned int    delay_samples[2];
    float           feedback;
    float           wet;
    float           dry;
    unsigned int    sample_rate;
} delay_data;

static void delay_process(sound_effect *effect, float *buffer,
                          unsigned int frames, unsigned int channels)
{
    delay_data *delay = effect->data;
    if (channels != 2 || !delay->size) return;

    for (unsigned int i = 0; i < frames; i++) {
        for (unsigned int ch = 0; ch < channels; ch++) {
            unsigned int read_pos = (delay->write_pos + delay->size - delay->delay_samples[ch]) % delay->size;

            float delayed   = delay->buffer[read_pos * channels + ch];
            float input     = buffer[i * channels + ch];
            float output    = input * delay->dry + delayed * delay->wet;

            delay->buffer[delay->write_pos * channels + ch] = input + delayed * delay->feedback;

            buffer[i * channels + ch] = output;
        }

        delay->write_pos = (delay->write_pos + 1) % delay->size;
    }
}

static cerr delay_init(sound_effect *effect, rc_init_opts(sound_effect) *opts)
{
    /*
     * Written to reject NaN, which fails every comparison; the delay times are
     * bounded as floats, before their conversions to unsigned int can overflow
     */
    float max_delay_ms = DELAY_BUFFER_MAX_SAMPLES * 1000.0f / ma_engine_get_sample_rate(&opts->ctx->engine);
    if (!(opts->delay_ms[0] >= 0.0f && opts->delay_ms[0] <= max_delay_ms))  return CERR_INVALID_ARGUMENTS;
    if (!(opts->delay_ms[1] >= 0.0f && opts->delay_ms[1] <= max_delay_ms))  return CERR_INVALID_ARGUMENTS;
    if (!(opts->feedback >= 0.0f && opts->feedback <= 1.0f))                return CERR_INVALID_ARGUMENTS;
    if (!(opts->wet_dry >= 0.0f && opts->wet_dry <= 1.0f))                  return CERR_INVALID_ARGUMENTS;

    delay_data *delay = mem_alloc(sizeof(*delay), .zero = 1);
    if (!delay) return CERR_NOMEM;

    delay->sample_rate = ma_engine_get_sample_rate(&opts->ctx->engine);
    delay->wet = opts->wet_dry;
    delay->dry = 1.0f - opts->wet_dry;
    delay->feedback = opts->feedback;

    delay->delay_samples[0] = (unsigned int)((opts->delay_ms[0] / 1000.0f) * delay->sample_rate);
    delay->delay_samples[1] = (unsigned int)((opts->delay_ms[1] / 1000.0f) * delay->sample_rate);

    unsigned int max_delay_samples = max(delay->delay_samples[0], delay->delay_samples[1]);

    if (max_delay_samples > DELAY_BUFFER_MAX_SAMPLES) {
        mem_free(delay);
        return CERR_INVALID_ARGUMENTS;
    }

    delay->size = max_delay_samples;
    effect->data = delay;
    return CERR_OK;
}

static void delay_done(sound_effect *effect)
{
    mem_free(effect->data);
}

/****************************************************************************
 * Sound effects: core
 ****************************************************************************/

static const sound_effect_desc sound_effect_descs[] = {
    [SOUND_EFFECT_REVERB] = {
        .name       = "reverb",
        .init       = reverb_init,
        .done       = reverb_done,
        .process    = reverb_process,
    },
    [SOUND_EFFECT_EQ] = {},
    [SOUND_EFFECT_COMPRESSOR] = {},
    [SOUND_EFFECT_DELAY] = {
        .name       = "delay",
        .init       = delay_init,
        .done       = delay_done,
        .process    = delay_process,
    },
};

static cerr sound_effect_make(struct ref *ref, void *_opts)
{
    rc_init_opts(sound_effect) *opts = _opts;
    auto effect = container_of(ref, sound_effect, ref);

    if (opts->type >= array_size(sound_effect_descs) || !sound_effect_descs[opts->type].process)
        return CERR_INVALID_ARGUMENTS;

    effect->desc = &sound_effect_descs[opts->type];
    list_init(&effect->entry);

    if (effect->desc->init)
        return effect->desc->init(effect, opts);

    return CERR_OK;
}

static void sound_effect_drop(struct ref *ref)
{
    auto effect = container_of(ref, sound_effect, ref);

    if (effect->desc && effect->desc->done)
        effect->desc->done(effect);

    list_del(&effect->entry);
}

DEFINE_REFCLASS2(sound_effect);

DEFINE_CLEANUP(sound_effect, if (*p) ref_put(*p))

static void sound_effect_chain_process_pcm_frames(ma_node *node, const float **frames_in,
                                                  ma_uint32 *frame_count_in, float **frames_out,
                                                  ma_uint32 *frame_count_out)
{
    sound_effect_chain *chain = (sound_effect_chain *)node;
    ma_uint32 channels = ma_node_get_output_channels(node, 0);

    /* sound_effect_chain_add() publishes an entry with a release store, which these loads pair with */
    struct list *pos = __atomic_load_n(&chain->effects.next, __ATOMIC_ACQUIRE);

    if (!chain->enabled || pos == &chain->effects) {
        ma_copy_pcm_frames(frames_out[0], frames_in[0], *frame_count_out,
                           ma_format_f32, channels);
        return;
    }

    ma_copy_pcm_frames(frames_out[0], frames_in[0], *frame_count_out, ma_format_f32, channels);

    for (; pos != &chain->effects; pos = __atomic_load_n(&pos->next, __ATOMIC_ACQUIRE)) {
        sound_effect *effect = list_entry(pos, sound_effect, entry);
        if (effect->desc && effect->desc->process)
            effect->desc->process(effect, frames_out[0], *frame_count_out, channels);
    }
}

static const ma_node_vtable sound_effect_chain_vtable = {
    .onProcess = sound_effect_chain_process_pcm_frames,
    .onGetRequiredInputFrameCount = NULL,
    .inputBusCount = 1,
    .outputBusCount = 1,
};

static cerr sound_effect_chain_make(struct ref *ref, void *_opts)
{
    sound_effect_chain *chain = container_of(ref, sound_effect_chain, ref);
    rc_init_opts(sound_effect_chain) *opts = _opts;

    if (!opts->ctx || !opts->ctx->started)
        return CERR_INVALID_ARGUMENTS;

    list_init(&chain->effects);
    list_init(&chain->sounds);
    chain->enabled = true;

    ma_uint32 channels = ma_engine_get_channels(&opts->ctx->engine);
    ma_node_config node_config = ma_node_config_init();
    node_config.vtable = &sound_effect_chain_vtable;
    node_config.pInputChannels = &channels;
    node_config.pOutputChannels = &channels;

    ma_result result = ma_node_init(ma_engine_get_node_graph(&opts->ctx->engine),
                                    &node_config, NULL, &chain->node);
    if (result != MA_SUCCESS)   return CERR_SOUND_NOT_LOADED;

    result = ma_node_attach_output_bus(&chain->node, 0, ma_engine_get_endpoint(&opts->ctx->engine), 0);
    if (result != MA_SUCCESS) {
        ma_node_uninit(&chain->node, NULL);
        return CERR_SOUND_NOT_LOADED;
    }

    list_append(&opts->ctx->chains, &chain->entry);

    return CERR_OK;
}

static void sound_effect_chain_drop(struct ref *ref)
{
    sound_effect_chain *chain = container_of(ref, sound_effect_chain, ref);

    while (!list_empty(&chain->sounds)) {
        auto s = list_first_entry(&chain->sounds, sound, chain_entry);
        sound_set_effect_chain(s, NULL);
    }

    /* uninit detaches the node and waits out the audio thread, which runs the effects until then */
    ma_node_uninit(&chain->node, nullptr);

    while (!list_empty(&chain->effects)) {
        sound_effect *effect = list_first_entry(&chain->effects, sound_effect, entry);
        ref_put(effect);
    }

    list_del(&chain->entry);
}

DEFINE_REFCLASS2(sound_effect_chain);

DEFINE_CLEANUP(sound_effect_chain, if (*p) ref_put(*p))

void sound_effect_chain_enable(sound_effect_chain *chain, bool enable)
{
    if (chain)  chain->enabled = enable;
}

void sound_effect_chain_add(sound_effect_chain *chain, sound_effect *effect)
{
    if (!chain || !effect)  return;

    ref_get(effect);

    /*
     * The audio thread walks the list as it plays: link the entry up before
     * the release store that makes it reachable
     */
    effect->entry.next = &chain->effects;
    effect->entry.prev = chain->effects.prev;
    __atomic_store_n(&chain->effects.prev->next, &effect->entry, __ATOMIC_RELEASE);
    chain->effects.prev = &effect->entry;
}

void sound_effect_chain_remove(sound_effect_chain *chain, sound_effect *effect)
{
    if (!chain || !effect)  return;

    /*
     * The audio thread may be inside the effect: detaching the chain's output
     * waits it out, at the cost of at most one block of the chain's output
     */
    ma_node *endpoint = ma_node_graph_get_endpoint(ma_node_get_node_graph(&chain->node));
    ma_node_detach_output_bus(&chain->node, 0);
    list_del(&effect->entry);
    ma_node_attach_output_bus(&chain->node, 0, endpoint, 0);

    ref_put(effect);
}

void sound_set_effect_chain(sound *sound, sound_effect_chain *chain)
{
    if (!sound) return;

    sound->effect_chain = chain;

    if (chain) {
        /* plug sound into chain */
        ma_node_attach_output_bus(&sound->sound, 0, &chain->node, 0);
        list_append(&chain->sounds, &sound->chain_entry);
    } else {
        /* plug sound directly into engine's endpoint */
        ma_node_attach_output_bus(&sound->sound, 0, ma_engine_get_endpoint(&sound->ctx->engine), 0);
        list_del(&sound->chain_entry);
    }
}

/****************************************************************************
 * Subtractive synthesizer
 ****************************************************************************/

/** SYNTH_CHUNK_FRAMES - frames rendered per pass through the oscillator, filter and envelope stages */
#define SYNTH_CHUNK_FRAMES 64

/** SYNTH_MIDI_A4 - MIDI note number of A4, the equal temperament reference */
#define SYNTH_MIDI_A4               69
#define SYNTH_MIDI_MAX              127
#define SYNTH_SEMITONES_PER_OCTAVE  12
#define SYNTH_CENTS_PER_OCTAVE      1200.0f

/* NaN fails every comparison, so these reject it along with the infinities */
static bool synth_nonneg(float x)                       { return isfinite(x) && x >= 0.0f; }
static bool synth_positive(float x)                     { return isfinite(x) && x > 0.0f; }
static bool synth_within(float x, float lo, float hi)   { return isfinite(x) && x >= lo && x <= hi; }

/* A pitch the oscillators can play: above DC and below Nyquist */
static bool synth_frequency_is_valid(float frequency, unsigned int sample_rate)
{
    return isfinite(frequency) && frequency > 0.0f && frequency < sample_rate / 2.0f;
}

static void synth_envelope_init(synth_envelope *env, const synth_envelope_config *config,
                                unsigned int sample_rate)
{
    env->config = *config;
    env->sample_rate = sample_rate;
    env->level = 0.0f;
    env->step = 0.0f;
    env->stage = SYNTH_ENV_IDLE;
}

/* Per-frame step that covers @delta in @ms; the whole of it at once under a frame */
static float synth_envelope_step(const synth_envelope *env, float ms, float delta)
{
    float frames = ms * env->sample_rate / 1000.0f;
    return frames < 1.0f ? delta : delta / frames;
}

/* Attack starts from the current level, so retriggering a sounding note doesn't click */
static void synth_envelope_gate_on(synth_envelope *env)
{
    env->stage = SYNTH_ENV_ATTACK;
    env->step = synth_envelope_step(env, env->config.attack_ms, 1.0f - env->level);
}

static void synth_envelope_gate_off(synth_envelope *env)
{
    if (env->stage == SYNTH_ENV_IDLE)   return;

    env->stage = SYNTH_ENV_RELEASE;
    env->step = synth_envelope_step(env, env->config.release_ms, env->level);
}

static float synth_envelope_next(synth_envelope *env)
{
    switch (env->stage) {
    case SYNTH_ENV_ATTACK:
        env->level += env->step;
        if (env->level >= 1.0f) {
            env->level = 1.0f;
            env->stage = SYNTH_ENV_DECAY;
            env->step = synth_envelope_step(env, env->config.decay_ms, 1.0f - env->config.sustain);
        }
        break;
    case SYNTH_ENV_DECAY:
        env->level -= env->step;
        if (env->level <= env->config.sustain) {
            env->level = env->config.sustain;
            env->stage = SYNTH_ENV_SUSTAIN;
        }
        break;
    case SYNTH_ENV_RELEASE:
        env->level -= env->step;
        if (env->level <= 0.0f) {
            env->level = 0.0f;
            env->stage = SYNTH_ENV_IDLE;
        }
        break;
    case SYNTH_ENV_SUSTAIN:
    case SYNTH_ENV_IDLE:
        break;
    }

    return env->level;
}

static float synth_detuned(const synth_oscillator_config *osc, float frequency)
{
    return frequency * exp2f(osc->detune / SYNTH_CENTS_PER_OCTAVE);
}

static void synth_note_on(synth *synth, float frequency)
{
    for (unsigned int i = 0; i < synth->config.nr_oscillators; i++)
        ma_waveform_set_frequency(&synth->osc[i], synth_detuned(&synth->config.oscillators[i], frequency));

    synth_envelope_gate_on(&synth->amp_env);
    synth_envelope_gate_on(&synth->filter_env);
}

static void synth_gate_off(synth *synth)
{
    synth_envelope_gate_off(&synth->amp_env);
    synth_envelope_gate_off(&synth->filter_env);
}

/* The next note from the built-in sequence or the callback; false when it's over */
static bool synth_next_note(synth *synth, synth_note *note)
{
    if (synth->config.next_note)    return synth->config.next_note(note, synth->config.next_note_data);

    if (synth->pos >= synth->config.nr_notes) {
        if (!synth->config.loop)    return false;

        synth->pos = 0;
    }

    *note = synth->config.notes[synth->pos++];

    return true;
}

/* The current note ran out: set up the next one, or hold the base frequencies */
static void synth_advance(synth *synth)
{
    if (!synth->config.next_note && !synth->config.notes) {
        synth->note_frames = UINT64_MAX;
        synth->gate_frames = 0;
        synth_envelope_gate_on(&synth->amp_env);
        synth_envelope_gate_on(&synth->filter_env);
        return;
    }

    synth_note note = {};
    if (!synth_next_note(synth, &note)) {
        synth->sequence_done = true;
        synth_gate_off(synth);
        return;
    }

    /*
     * The callback's notes are not checked ahead of time: a duration the
     * synth cannot use lasts one frame, or the sequence would be pulled dry
     * in one callback, and a pitch it cannot play is a rest. 2^63 frames
     * keeps the float to integer conversion defined.
     */
    float duration_ms = synth_positive(note.duration_ms) ? note.duration_ms : 0.0f;
    ma_uint64 frames = (ma_uint64)fminf(duration_ms * synth->sample_rate / 1000.0f + 0.5f, 0x1p63f);
    synth->note_frames = max(frames, 1);

    if (synth_frequency_is_valid(note.frequency, synth->sample_rate)) {
        ma_uint64 gate = (ma_uint64)(synth->note_frames * synth->config.gate + 0.5f);
        synth->gate_frames = min(max(gate, 1), synth->note_frames);
        synth_note_on(synth, note.frequency);
    } else {
        /* a rest: the previous note keeps releasing, nothing new to gate */
        synth->gate_frames = 0;
    }
}

/*
 * Oscillators and filter run even while the amplitude envelope is idle, so a
 * note's phase and the filter state don't depend on where the host's blocks
 * happened to fall
 */
static void synth_render(synth *synth, float *out, ma_uint32 frames)
{
    float mix[SYNTH_CHUNK_FRAMES] = {};
    float osc[SYNTH_CHUNK_FRAMES];

    for (unsigned int i = 0; i < synth->config.nr_oscillators; i++) {
        ma_waveform_read_pcm_frames(&synth->osc[i], osc, frames, nullptr);
        for (ma_uint32 f = 0; f < frames; f++)  mix[f] += osc[f];
    }

    /*
     * The filter envelope moves the cutoff every SYNTH_CHUNK_FRAMES frames,
     * counted from the synth's start rather than from the chunk, which may
     * be cut short by a note or block end: reinit only recomputes the biquad
     * coefficients and keeps its state, so the sweep is stepped without
     * discontinuities
     */
    if (!(synth->frames % SYNTH_CHUNK_FRAMES)) {
        float cutoff = synth->config.filter.cutoff *
                       exp2f(synth->config.filter.env_amount * synth->filter_env.level);
        cutoff = clampf(cutoff, SYNTH_CUTOFF_MIN_HZ, synth->sample_rate * SYNTH_CUTOFF_MAX_RATIO);

        auto lpf_config = ma_lpf2_config_init(ma_format_f32, 1, synth->sample_rate, cutoff,
                                              synth->config.filter.resonance);
        ma_lpf2_reinit(&lpf_config, &synth->filter);
    }
    ma_lpf2_process_pcm_frames(&synth->filter, mix, mix, frames);

    for (ma_uint32 f = 0; f < frames; f++) {
        float sample = mix[f] * synth_envelope_next(&synth->amp_env) * synth->config.gain;
        synth_envelope_next(&synth->filter_env);

        for (unsigned int ch = 0; ch < synth->channels; ch++)   out[f * synth->channels + ch] = sample;
    }
}

/*
 * Runs on the audio thread. Output is rendered in chunks that never straddle
 * a gate end or a note end, so envelope and note changes land on the exact
 * frame; the chunks are aligned to the synth's own frame count, so the same
 * patch renders the same samples whatever the device's period size.
 */
static void synth_process_pcm_frames(ma_node *node, const float **frames_in, ma_uint32 *frame_count_in,
                                     float **frames_out, ma_uint32 *frame_count_out)
{
    synth *synth = (struct synth *)node;
    ma_uint32 frames = *frame_count_out;

    for (ma_uint32 done = 0; done < frames; ) {
        if (!synth->sequence_done && !synth->note_frames)   synth_advance(synth);

        ma_uint64 n = min(frames - done, SYNTH_CHUNK_FRAMES - synth->frames % SYNTH_CHUNK_FRAMES);
        if (!synth->sequence_done) {
            n = min(n, synth->note_frames);
            if (synth->gate_frames)     n = min(n, synth->gate_frames);
        }

        synth_render(synth, frames_out[0] + done * synth->channels, n);
        done += n;
        synth->frames += n;

        if (synth->sequence_done)       continue;

        synth->note_frames -= n;
        if (synth->gate_frames) {
            synth->gate_frames -= n;
            if (!synth->gate_frames)    synth_gate_off(synth);
        }
    }
}

static const ma_node_vtable synth_vtable = {
    .onProcess      = synth_process_pcm_frames,
    .inputBusCount  = 0,
    .outputBusCount = 1,
};

static const ma_waveform_type synth_waveform_types[] = {
    [SYNTH_WAVE_SINE]       = ma_waveform_type_sine,
    [SYNTH_WAVE_SQUARE]     = ma_waveform_type_square,
    [SYNTH_WAVE_TRIANGLE]   = ma_waveform_type_triangle,
    [SYNTH_WAVE_SAWTOOTH]   = ma_waveform_type_sawtooth,
};

static bool synth_envelope_config_is_valid(const synth_envelope_config *env)
{
    return synth_nonneg(env->attack_ms) && synth_nonneg(env->decay_ms) && synth_nonneg(env->release_ms) &&
           synth_within(env->sustain, 0.0f, 1.0f);
}

static bool synth_note_is_valid(const synth_note *note, unsigned int sample_rate)
{
    if (note->frequency != 0.0f && !synth_frequency_is_valid(note->frequency, sample_rate)) return false;

    return synth_positive(note->duration_ms);
}

/* The reason names the field, so a script's bad value is reported as such */
static cerr synth_bad(const char *field)
{
    return CERR_INVALID_ARGUMENTS_REASON(.fmt = "bad %s", .arg0 = field);
}

static cerr synth_config_check(const synth_config *config, unsigned int sample_rate)
{
    if (!config->nr_oscillators)                            return synth_bad("nr_oscillators");
    if (config->nr_oscillators > SYNTH_MAX_OSCILLATORS)     return synth_bad("nr_oscillators");
    if (config->notes && config->next_note)                 return synth_bad("notes with next_note");
    if (!config->notes != !config->nr_notes)                return synth_bad("nr_notes");

    bool sequenced = config->notes || config->next_note;

    for (unsigned int i = 0; i < config->nr_oscillators; i++) {
        auto osc = &config->oscillators[i];

        if (osc->waveform >= array_size(synth_waveform_types))                          return synth_bad("waveform");
        if (!synth_nonneg(osc->gain))                                                   return synth_bad("osc gain");
        /* written to reject NaN, which passes a greater-than test */
        if (!(fabsf(osc->detune) <= SYNTH_DETUNE_MAX_CENTS))                            return synth_bad("detune");
        /* the base frequency is the pitch when there is no sequence to supply one */
        if (!sequenced && !synth_frequency_is_valid(osc->frequency, sample_rate))       return synth_bad("frequency");
    }

    for (unsigned int i = 0; i < config->nr_notes; i++)
        if (!synth_note_is_valid(&config->notes[i], sample_rate))   return synth_bad("note");

    float cutoff_max = sample_rate * SYNTH_CUTOFF_MAX_RATIO;
    if (!synth_within(config->filter.cutoff, SYNTH_CUTOFF_MIN_HZ, cutoff_max))      return synth_bad("cutoff");
    if (!synth_positive(config->filter.resonance))                                  return synth_bad("resonance");
    if (!isfinite(config->filter.env_amount))                                       return synth_bad("env_amount");
    if (!synth_envelope_config_is_valid(&config->amp_env))                          return synth_bad("amp_env");
    if (!synth_envelope_config_is_valid(&config->filter_env))                       return synth_bad("filter_env");
    if (!synth_nonneg(config->gain))                                                return synth_bad("gain");
    if (sequenced && !(synth_positive(config->gate) && config->gate <= 1.0f))       return synth_bad("gate");

    return CERR_OK;
}

static cerr synth_make(struct ref *ref, void *_opts)
{
    rc_init_opts(synth) *opts = _opts;
    if (!opts->ctx || !opts->ctx->started || !opts->config) return CERR_INVALID_ARGUMENTS;

    synth *synth = container_of(ref, struct synth, ref);
    synth->ctx = opts->ctx;
    synth->config = *opts->config;
    synth->sample_rate = ma_engine_get_sample_rate(&opts->ctx->engine);
    synth->channels = ma_engine_get_channels(&opts->ctx->engine);
    synth->note_frames = 0;
    synth->gate_frames = 0;
    synth->frames = 0;
    synth->pos = 0;
    synth->sequence_done = false;
    list_init(&synth->entry);

    CERR_RET_CERR(synth_config_check(&synth->config, synth->sample_rate));

    for (unsigned int i = 0; i < synth->config.nr_oscillators; i++) {
        auto osc = &synth->config.oscillators[i];
        auto wave_config = ma_waveform_config_init(ma_format_f32, 1, synth->sample_rate,
                                                   synth_waveform_types[osc->waveform], osc->gain,
                                                   synth_detuned(osc, osc->frequency));
        if (ma_waveform_init(&wave_config, &synth->osc[i]) != MA_SUCCESS)   return CERR_INITIALIZATION_FAILED;
    }

    auto lpf_config = ma_lpf2_config_init(ma_format_f32, 1, synth->sample_rate, synth->config.filter.cutoff,
                                          synth->config.filter.resonance);
    if (ma_lpf2_init(&lpf_config, nullptr, &synth->filter) != MA_SUCCESS)   return CERR_INITIALIZATION_FAILED;

    synth_envelope_init(&synth->amp_env, &synth->config.amp_env, synth->sample_rate);
    synth_envelope_init(&synth->filter_env, &synth->config.filter_env, synth->sample_rate);

    ma_uint32 channels = synth->channels;
    ma_node_config node_config = ma_node_config_init();
    node_config.vtable = &synth_vtable;
    node_config.pOutputChannels = &channels;
    node_config.initialState = ma_node_state_stopped;

    ma_result result = ma_node_init(ma_engine_get_node_graph(&opts->ctx->engine), &node_config, nullptr,
                                    &synth->node);
    if (result != MA_SUCCESS)   goto err_filter;

    result = ma_node_attach_output_bus(&synth->node, 0, ma_engine_get_endpoint(&opts->ctx->engine), 0);
    if (result != MA_SUCCESS)   goto err_node;

    list_append(&opts->ctx->synths, &synth->entry);

    return CERR_OK;

err_node:
    ma_node_uninit(&synth->node, nullptr);

err_filter:
    ma_lpf2_uninit(&synth->filter, nullptr);

    return CERR_INITIALIZATION_FAILED;
}

static void synth_drop(struct ref *ref)
{
    synth *synth = container_of(ref, struct synth, ref);

    ma_node_uninit(&synth->node, nullptr);
    ma_lpf2_uninit(&synth->filter, nullptr);
    list_del(&synth->entry);
}

DEFINE_REFCLASS2(synth);

DEFINE_CLEANUP(synth, if (*p) ref_put(*p))

void synth_start(synth *synth)
{
    /* a running synth's state belongs to the audio thread */
    if (ma_node_get_state(&synth->node) == ma_node_state_started)   return;

    synth->sequence_done = false;
    synth->note_frames = 0;
    synth->gate_frames = 0;
    synth->pos = 0;
    ma_node_set_state(&synth->node, ma_node_state_started);
}

void synth_stop(synth *synth)
{
    ma_node_set_state(&synth->node, ma_node_state_stopped);
}

bool synth_is_playing(synth *synth)
{
    return ma_node_get_state(&synth->node) == ma_node_state_started;
}

cres(float) synth_midi_frequency(int midi, float tuning)
{
    if (midi < 0 || midi > SYNTH_MIDI_MAX || !synth_nonneg(tuning)) return cres_error(float, CERR_INVALID_ARGUMENTS);

    if (tuning == 0.0f)     tuning = SYNTH_TUNING_A440;

    return cres_val(float, tuning * exp2f((float)(midi - SYNTH_MIDI_A4) / SYNTH_SEMITONES_PER_OCTAVE));
}

cres(float) synth_pitch_frequency(const char *pitch, float tuning)
{
    /* semitones above C of the letters A..G */
    static const int letter_semitones[] = { 9, 11, 0, 2, 4, 5, 7 };

    if (!pitch)     return cres_error(float, CERR_INVALID_ARGUMENTS);

    int letter = toupper((unsigned char)pitch[0]);
    if (letter < 'A' || letter > 'G')   return cres_error(float, CERR_PARSE_FAILED);

    int semitone = letter_semitones[letter - 'A'];
    const char *p = pitch + 1;
    for (; *p == '#' || *p == 'b'; p++) semitone += *p == '#' ? 1 : -1;

    /* strtol() would also take leading whitespace and a plus sign */
    if (*p != '-' && !isdigit((unsigned char)*p))   return cres_error(float, CERR_PARSE_FAILED);

    char *end;
    long octave = strtol(p, &end, 10);
    if (*end)       return cres_error(float, CERR_PARSE_FAILED);

    /* strtol() saturates, so bound the octave before the int arithmetic below can overflow */
    long max_octave = SYNTH_MIDI_MAX / SYNTH_SEMITONES_PER_OCTAVE;
    if (octave < -1 || octave > max_octave)     return cres_error(float, CERR_INVALID_ARGUMENTS);

    /* scientific pitch notation: C-1 is MIDI 0, so octave n starts at (n + 1) * 12 */
    return synth_midi_frequency((int)(octave + 1) * SYNTH_SEMITONES_PER_OCTAVE + semitone, tuning);
}

cres(float) synth_note_duration_ms(float bpm, double duration)
{
    if (!synth_positive(bpm) || !isfinite(duration) || duration <= 0.0)
        return cres_error(float, CERR_INVALID_ARGUMENTS);

    /* a quarter note (0.25) is one beat of 60000 / bpm ms, so a whole note is four */
    return cres_val(float, (float)(duration * 4.0 * 60000.0 / bpm));
}

#ifdef CLAP_TESTS
cresp(sound_context) sound_init_headless(unsigned int sample_rate, unsigned int channels)
{
    LOCAL_SET(sound_context, ctx) = mem_alloc(sizeof(*ctx), .zero = 1);
    if (!ctx)   return cresp_error(sound_context, CERR_NOMEM);

    ctx->master_music = 1.0f;
    ctx->master_sfx   = 1.0f;
    list_init(&ctx->sounds);
    list_init(&ctx->chains);
    list_init(&ctx->synths);

    auto config = ma_engine_config_init();
    config.noDevice   = MA_TRUE;
    config.sampleRate = sample_rate;
    config.channels   = channels;
    ma_result result = ma_engine_init(&config, &ctx->engine);
    if (result != MA_SUCCESS)   return cresp_error(sound_context, CERR_INITIALIZATION_FAILED);

    ctx->started = true;

    return cresp_val(sound_context, NOCU(ctx));
}

size_t sound_read_frames(sound_context *ctx, float *out, size_t frames)
{
    ma_uint64 read = 0;

    ma_engine_read_pcm_frames(&ctx->engine, out, frames, &read);

    return read;
}
#endif /* CLAP_TESTS */

/****************************************************************************
 * Effect sounds (SFX)
 ****************************************************************************/

typedef struct sfx {
    sound           *sound;
    char            *action;
    struct list     entry;
    sfx_container   *sfxc;
} sfx;

static int sfx_handle_command(struct clap_context *ctx, struct message *m, void *data)
{
    if (!m->cmd.sound_ready)        return MSG_HANDLED;

    sfx_container *sfxc = data;
    if (list_empty(&sfxc->list))    return MSG_HANDLED;
    if (!sfxc->on_add)              return MSG_HANDLED;

    sfx *sfx = list_first_entry(&sfxc->list, struct sfx, entry);
    /* should not happen on sound_ready event, but to be safe */
    if (!sfx->sound->ctx->started)  return MSG_STOP;

    list_for_each_entry(sfx, &sfxc->list, entry)
        sfxc->on_add(sfx->sound, sfxc->data);

    return MSG_HANDLED;
}

void sfx_container_init(sfx_container *sfxc)
{
    list_init(&sfxc->list);
}

static void sfx_container_add(sfx_container *sfxc, sfx *sfx)
{
    if (list_empty(&sfxc->list))
        subscribe(sfx->sound->ctx->clap_ctx, MT_COMMAND, sfx_handle_command, sfxc);

    list_append(&sfxc->list, &sfx->entry);
    sfx->sfxc = sfxc;

    /*
     * if sound engine has started, run sfxc->on_add(), otherwise
     * sfx_handle_command() will call it on everything
     */
    auto ctx = sfx->sound->ctx;
    if (ctx->started && sfxc->on_add)
        sfxc->on_add(sfx->sound, sfxc->data);
}

static void sfx_done(sfx *sfx)
{
    if (sfx->sound)
        ref_put(sfx->sound);

    list_del(&sfx->entry);
    mem_free(sfx->action);
    mem_free(sfx);
}

void sfx_container_clearout(sfx_container *sfxc)
{
    if (list_empty(&sfxc->list))    return;

    sfx *s = list_first_entry(&sfxc->list, struct sfx, entry);
    unsubscribe(s->sound->ctx->clap_ctx, MT_COMMAND, sfxc);

    while (!list_empty(&sfxc->list)) {
        sfx *del = list_first_entry(&sfxc->list, struct sfx, entry);
        sfx_done(del);
    }
}

DEFINE_CLEANUP(sfx, if (*p) { sfx_done(*p); });

cresp(sfx) sfx_new(sfx_container *sfxc, const char *name, const char *file, sound_context *ctx)
{
    LOCAL_SET(sfx, sfx) = mem_alloc(sizeof(*sfx), .zero = 1);
    if (!sfx)
        return cresp_error(sfx, CERR_NOMEM);

    list_init(&sfx->entry); /* if sfx_done() is called on a partial sfx */
    sfx->action = strdup(name);
    if (!sfx->action)
        return cresp_error(sfx, CERR_NOMEM);

    sound *s;
    list_for_each_entry(s, &ctx->sounds, entry)
        if (!strcmp(s->name, file)) {
            sfx->sound = ref_get(s);
            goto found;
        }

    sfx->sound = CRES_RET_T(
        ref_new_checked(sound, .name = file, .ctx = ctx, .category = SOUND_CAT_SFX),
        sfx);

found:
    sound_set_gain(sfx->sound, 1.0);
    sfx_container_add(sfxc, sfx);

    return cresp_val(sfx, NOCU(sfx));
}

sfx *sfx_get(sfx_container *sfxc, const char *name)
{
    sfx *sfx;

    list_for_each_entry(sfx, &sfxc->list, entry)
        if (!strcmp(sfx->action, name))
            return sfx;

    return NULL;
}

void sfx_play(sfx *sfx)
{
    sound_play(sfx->sound);
}

void sfx_play_by_name(sfx_container *sfxc, const char *name)
{
    sfx *sfx = sfx_get(sfxc, name);

    if (!sfx)
        return;

    sound_play(sfx->sound);
}
