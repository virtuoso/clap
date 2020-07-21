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
    uchar           *buf;
    struct list     entry;
    struct ref      ref;
};

void sound_init(void);
void sound_play(void);

#endif /* __CLAP_SOUND_H__ */
