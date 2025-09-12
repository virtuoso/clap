// SPDX-License-Identifier: Apache-2.0
#include "messagebus.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "miniaudio.h"

#include "object.h"
#include "librarian.h"
#include "logger.h"
#include "sound.h"

typedef struct sound_context {
    ma_context  context;
    ma_device   device;
    ma_engine   engine;
    bool        started;
    struct list sounds;
    struct list chains;
    clap_context *clap_ctx;
} sound_context;

typedef struct sound {
    char                *name;
    sound_context       *ctx;
    ma_sound            sound;
    ma_audio_buffer     buffer;
    float               *pcm;
    unsigned int        nr_channels;
    float               gain;
    bool                looping;
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
    bool            enabled;
    struct ref      ref;
    struct list     entry;
};

 typedef struct ov_cb_data {
    void    *buf;
    size_t  size;
    off_t   off;
} ov_cb_data;

void sound_set_gain(sound *sound, float gain)
{
    sound->gain = gain;

    if (!sound->ctx->started)   return;

    ma_sound_set_min_gain(&sound->sound, gain);
    ma_sound_set_max_gain(&sound->sound, gain);
}

float sound_get_gain(sound *sound)
{
    return sound->gain;
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

    // auto sound_config = ma_sound_config_init();
    // sound_config.flags = MA_SOUND_FLAG_DECODE;
    // sound_config.pDataSource = cb_data.buf;
    // sound_config.p
    // ma_data_source *data_source;
    // ma_data_source_config src_config = ma_data_source_config();
    // ma_data_source_init(src_config, data_source);
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

    if (opts->ctx->started)
        CERR_RET(do_sound_make(sound, opts->ctx), { free(sound->name); return __cerr; });

    list_append(&opts->ctx->sounds, &sound->entry);

    return CERR_OK;
}

static void sound_drop(struct ref *ref)
{
    sound *sound = container_of(ref, struct sound, ref);

    if (sound->ctx->started) {
        if (sound->effect_chain)
            sound_set_effect_chain(sound, NULL);
        ma_audio_buffer_uninit(&sound->buffer);
        ma_sound_uninit(&sound->sound);
        mem_free(sound->pcm);
    }

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
    if (!sound->ctx->started)   return;

    if (ma_sound_is_playing(&sound->sound))
        ma_sound_seek_to_pcm_frame(&sound->sound, 0);

    ma_sound_start(&sound->sound);
}

static void do_sound_init(sound_context *ctx)
{
    // auto resmng_config = ma_resource_manager_config_init();
    // resmng_config.decodedFormat = ma_format_f32;
    // resmng_config.decodedChannels = 0;
    // resmng_config.decodedSampleRate = 48000;//44100;

    // ma_resource_manager resmng;
    // auto result = ma_resource_manager_init(&resmng_config, &resmng);
    // if (result != MA_SUCCESS)   return cresp_error(sound_context, CERR_SOUND_NOT_LOADED); // XXX CERR_

    // result = ma_context_init(NULL, 0, NULL, &ctx->context);
    // if (result != MA_SUCCESS)   goto err_resmgr;

    // ma_device_info *device_infos;
    // ma_uint32 device_count;
    // result = ma_context_get_devices(&ctx->context, &device_infos, &device_count, NULL, NULL);
    // if (result != MA_SUCCESS)   goto err_context;

    // ma_device_id *dev_id = &device_infos[0].id;
    // for (ma_uint32 i = 0; i < device_count; i++) {
    //     dbg(" -> device%u: %s\n", i, device_infos[i].name);
    //     if (device_infos[i].isDefault)  dev_id = &device_infos[i].id;
    // }

    // auto dev_config = ma_device_config_init(ma_device_type_playback);
    // dev_config.playback.pDeviceID   = dev_id;
    // dev_config.playback.format      = resmng.config.decodedFormat;
    // dev_config.playback.channels    = 0;
    // dev_config.sampleRate           = resmng.config.decodedSampleRate;

    // result = ma_device_init(&ctx->context, &dev_config, &ctx->device);
    // if (result != MA_SUCCESS)   goto err_devices;

    // ma_engine_config config = ma_engine_config_init();
    // config.channels = 2;
    // config.sampleRate = 44100;
    // config.pDevice          = &ctx->device;
    // config.pResourceManager = &resmng;
    // config.noAutoStart      = MA_TRUE;

    // result = ma_engine_init(&config, &ctx->engine);
    auto result = ma_engine_init(NULL, &ctx->engine);
    if (result != MA_SUCCESS)   goto err_engine;//err_device;

    result = ma_engine_start(&ctx->engine);
    if (result != MA_SUCCESS)   goto err_engine;

    ctx->started = true;
    sound *s;
    list_for_each_entry(s, &ctx->sounds, entry) {
        do_sound_make(s, ctx);
        ma_sound_set_looping(&s->sound, s->looping);
        ma_sound_set_min_gain(&s->sound, s->gain);
        ma_sound_set_max_gain(&s->sound, s->gain);
    }
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
// err_device:
//     ma_device_uninit(&ctx->device);
// err_devices:
//     free(device_infos);
// err_context:
//     ma_context_uninit(&ctx->context);
// err_resmgr:
//     ma_resource_manager_uninit(&resmng);
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

cresp(sound_context) sound_init(clap_context *clap_ctx)
{
    LOCAL_SET(sound_context, ctx) = mem_alloc(sizeof(*ctx), .zero = 1);
    if (!ctx)   return cresp_error(sound_context, CERR_NOMEM);

    ctx->clap_ctx = clap_ctx;
    list_init(&ctx->sounds);
    list_init(&ctx->chains);

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

    if (ctx->started) {
        ma_engine_stop(&ctx->engine);
        ma_engine_uninit(&ctx->engine);
        ma_device_uninit(&ctx->device);
        ma_context_uninit(&ctx->context);
    }

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

static cerr reverb_init(sound_effect *effect, rc_init_opts(sound_effect) *opts)
{
    if (opts->reverb_type >= array_size(reverb_types))
        return CERR_INVALID_ARGUMENTS;
    if (opts->room_size < 0.0f || opts->room_size > 1.0f)
        return CERR_INVALID_ARGUMENTS;
    if (opts->damping < 0.0f || opts->damping > 1.0f)
        return CERR_INVALID_ARGUMENTS;
    if (opts->wet_dry < 0.0f || opts->wet_dry > 1.0f)
        return CERR_INVALID_ARGUMENTS;

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
    if (opts->delay_ms[0] < 0.0f || opts->delay_ms[1] < 0.0f)
        return CERR_INVALID_ARGUMENTS;
    if (opts->feedback < 0.0f || opts->feedback > 1.0f)
        return CERR_INVALID_ARGUMENTS;
    if (opts->wet_dry < 0.0f || opts->wet_dry > 1.0f)
        return CERR_INVALID_ARGUMENTS;

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

    if (!chain->enabled || list_empty(&chain->effects)) {
        ma_copy_pcm_frames(frames_out[0], frames_in[0], *frame_count_out,
                           ma_format_f32, channels);
        return;
    }

    ma_copy_pcm_frames(frames_out[0], frames_in[0], *frame_count_out, ma_format_f32, channels);

    sound_effect *effect;
    list_for_each_entry(effect, &chain->effects, entry) {
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

    while (!list_empty(&chain->effects)) {
        sound_effect *effect = list_first_entry(&chain->effects, sound_effect, entry);
        ref_put(effect);
    }

    ma_node_uninit(&chain->node, NULL);
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
    list_append(&chain->effects, &effect->entry);
    ref_get(effect);
}

void sound_effect_chain_remove(sound_effect_chain *chain, sound_effect *effect)
{
    if (!chain || !effect)  return;
    list_del(&effect->entry);
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

    sfx->sound = CRES_RET_T(ref_new_checked(sound, .name = file, .ctx = ctx), sfx);

found:
    sound_set_gain(sfx->sound, 0.4);
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
