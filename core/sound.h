/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SOUND_H__
#define __CLAP_SOUND_H__

struct sound;

void sound_init(void);
void sound_done(void);
struct sound *sound_load(const char *name);
float sound_get_gain(struct sound *sound);
void sound_set_gain(struct sound *sound, float gain);
void sound_set_looping(struct sound *sound, bool looping);
void sound_play(struct sound *sound);

#endif /* __CLAP_SOUND_H__ */
