/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SOUND_H__
#define __CLAP_SOUND_H__

#include "object.h"
#include "util.h"

typedef struct sound_context sound_context;
typedef struct sound sound;
typedef struct sfx sfx;

cresp_ret(sound_context);

typedef struct sfx_container {
    struct list list;
} sfx_container;

DEFINE_REFCLASS_INIT_OPTIONS(sound,
    const char      *name;
    sound_context   *ctx;
);
DECLARE_REFCLASS(sound);

cresp(sound_context) sound_init(void);
void sound_done(sound_context *ctx);
float sound_get_gain(sound *sound);
void sound_set_gain(sound *sound, float gain);
void sound_set_looping(sound *sound, bool looping);
void sound_play(sound *sound);

cresp_ret(sfx);
void sfx_container_init(sfx_container *sfxc);
void sfx_container_clearout(sfx_container *sfxc);
cresp(sfx) sfx_new(sfx_container *sfxc, const char *name, const char *file, sound_context *ctx);
sfx *sfx_get(sfx_container *sfxc, const char *name);
void sfx_play(sfx *sfx);
void sfx_play_by_name(sfx_container *sfxc, const char *name);

#endif /* __CLAP_SOUND_H__ */
