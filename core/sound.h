#ifndef __CLAP_SOUND_H__
#define __CLAP_SOUND_H__

#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
//#include <OpenAL/alut.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
//#include <AL/alut.h>
#endif /* __APPLE__ */

#include "object.h"

struct sound {
    unsigned int    nr_channels;
    ALsizei         size, freq;
    ALenum          format;
    ALuint          buffer_idx;
    ALuint          source_idx;
    float           gain;
    uchar           *buf;
    struct list     entry;
    struct ref      ref;
};

void sound_init(void);
void sound_done(void);
struct sound *sound_load(const char *name);
float sound_get_gain(struct sound *sound);
void sound_set_gain(struct sound *sound, float gain);
void sound_set_looping(struct sound *sound, bool looping);
void sound_play(struct sound *sound);

#endif /* __CLAP_SOUND_H__ */
