/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SOUND_H__
#define __CLAP_SOUND_H__

typedef struct sound_context sound_context;
typedef struct sound sound;

DEFINE_REFCLASS_INIT_OPTIONS(sound,
    const char      *name;
    sound_context   *ctx;
);
DECLARE_REFCLASS(sound);

sound_context *sound_init(void);
void sound_done(sound_context *ctx);
float sound_get_gain(sound *sound);
void sound_set_gain(sound *sound, float gain);
void sound_set_looping(sound *sound, bool looping);
void sound_play(sound *sound);

#endif /* __CLAP_SOUND_H__ */
