/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SOUND_H__
#define __CLAP_SOUND_H__

typedef struct sound_context sound_context;
typedef struct sound sound;

sound_context *sound_init(void);
void sound_done(sound_context *ctx);
struct sound *sound_load(sound_context *ctx, const char *name);
float sound_get_gain(sound *sound);
void sound_set_gain(sound *sound, float gain);
void sound_set_looping(sound *sound, bool looping);
void sound_play(sound *sound);

#endif /* __CLAP_SOUND_H__ */
