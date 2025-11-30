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
    auto result = ma_engine_init(NULL, &ctx->engine);
    if (result != MA_SUCCESS)   return;

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
static int sound_handle_input(struct message *m, void *data)
{
    sound_context *ctx = data;

    if (ctx->started)   return MSG_HANDLED;

    if (m->input.mouse_click /*|| m->input.mouse_move */|| m->input.keyboard)
        do_sound_init(ctx);

    return MSG_HANDLED;
}
#endif /* CONFIG_BROWSER */

DEFINE_CLEANUP(sound_context, if (*p) mem_free(*p))

cresp(sound_context) sound_init(void)
{
    LOCAL_SET(sound_context, ctx) = mem_alloc(sizeof(*ctx), .zero = 1);
    if (!ctx)   return cresp_error(sound_context, CERR_NOMEM);

    list_init(&ctx->sounds);
    list_init(&ctx->chains);

#ifdef CONFIG_BROWSER
    subscribe(MT_INPUT, sound_handle_input, ctx);
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
 * Sound effects: core
 ****************************************************************************/

static const sound_effect_desc sound_effect_descs[] = {
    [SOUND_EFFECT_REVERB] = {},
    [SOUND_EFFECT_EQ] = {},
    [SOUND_EFFECT_COMPRESSOR] = {},
    [SOUND_EFFECT_DELAY] = {},
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
    sound       *sound;
    char        *action;
    struct list entry;
} sfx;

void sfx_container_init(sfx_container *sfxc)
{
    list_init(&sfxc->list);
}

static void sfx_container_add(sfx_container *sfxc, sfx *sfx)
{
    list_append(&sfxc->list, &sfx->entry);
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
    while (!list_empty(&sfxc->list)) {
        sfx *sfx = list_first_entry(&sfxc->list, struct sfx, entry);
        sfx_done(sfx);
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
