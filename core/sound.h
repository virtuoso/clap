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

struct clap_context;

cresp_ret(sound_context);

typedef struct sfx_container {
    struct list list;
    void        (*on_add)(sound *, void *);
    void        *data;
} sfx_container;

DEFINE_REFCLASS_INIT_OPTIONS(sound,
    const char      *name;
    sound_context   *ctx;
);
DECLARE_REFCLASS(sound);

cresp(sound_context) sound_init(struct clap_context *clap_ctx);
void sound_done(sound_context *ctx);
float sound_get_gain(sound *sound);
void sound_set_gain(sound *sound, float gain);
void sound_set_looping(sound *sound, bool looping);
void sound_play(sound *sound);

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

#endif /* __CLAP_SOUND_H__ */
