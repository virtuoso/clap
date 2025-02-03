// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

#include "object.h"
#include "librarian.h"
#include "logger.h"
#include "sound.h"

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

//#define NUM_BUFFERS 1
#define NUM_SOURCES 1
#define NUM_ENVIRONMENTS 1

static ALCdevice * device;
static ALCcontext *context;

ALfloat listenerPos[]={0.0,0.0,0.0};
ALfloat listenerVel[]={0.0,0.0,0.0};
ALfloat listenerOri[]={0.0,0.0,1.0, 0.0,1.0,0.0};
ALfloat source0Pos[]={ 0.0, 0.0, 0.0};
ALfloat source0Vel[]={ 0.0, 0.0, 0.0};

//ALuint  buffer[NUM_BUFFERS];
ALuint  source[NUM_SOURCES];
ALuint  environment[NUM_ENVIRONMENTS];

static DECLARE_LIST(sounds);

/* ffmpeg -i asset/morning.wav -codec:a libvorbis -ar 44100 asset/morning.ogg */

static size_t ogg_read(void *ptr, size_t size, size_t nmemb, void *datasource)
{
    return fread(ptr, size, nmemb, datasource);
}

/* OV_CALLBACKS_NOCLOSE have the wrong fread() */
static const ov_callbacks ogg_callbacks = {
    .read_func  = ogg_read,
};

/* Non-stdio load: https://xiph.org/vorbis/doc/vorbisfile/callbacks.html */
#define BUFSZ (4096*1024)
static int parse_ogg(struct sound *sound, const char *uri)
{
    int eof = 0, current_section;
    long ret, offset = 0, bufsz = 0;
    OggVorbis_File vf;
    vorbis_info *vi;
    LOCAL(FILE, f);
    char **ptr;

    CHECK(f = fopen(uri, "r"));
    if (ov_open_callbacks(f, &vf, NULL, 0, ogg_callbacks) < 0) {
        err("can't open '%s'\n", uri);
        return -1;
    }
    
    ptr = ov_comment(&vf, -1)->user_comments;
    vi = ov_info(&vf,-1);
    while (*ptr) {
        dbg(" => %s\n", *ptr);
        ++ptr;
    }

    dbg("bitstream is %d channel, %ldHz\n", vi->channels, vi->rate);
    dbg("Encoded by: %s\n", ov_comment(&vf,-1)->vendor);

    sound->nr_channels = vi->channels;
    sound->freq = vi->rate;
    sound->format = AL_FORMAT_STEREO16; /* XXX */
    while (!eof) {
        if (!bufsz) {
            bufsz = BUFSZ;
            sound->buf = realloc(sound->buf, sound->size + BUFSZ);
        }
        ret = ov_read(&vf, (char *)sound->buf + offset, bufsz, 0, 2, 1, &current_section);
        sound->size += ret;
        offset += ret;
        bufsz -= ret;
        //dbg("read %ld (%ld), size %zu\n", ret, BUFSZ, sound->size);
        if (ret == 0)
            eof = 1;
    }
    dbg("size: %d\n", sound->size);
    alBufferData(sound->buffer_idx, sound->format, sound->buf,
                 sound->size, sound->freq);
    mem_free(sound->buf);
    sound->buf = NULL;
    ov_clear(&vf);

    return 0;
}

static int parse_wav(struct sound *sound, const char *uri)
{
    LOCAL(FILE, f);
    struct stat st;
    int offset, bits;

    CHECK(f = fopen(uri, "r"));

    fstat(fileno(f), &st);
    sound->size = st.st_size;

    sound->buf = mem_alloc(st.st_size, .fatal_fail = 1);
    CHECK_VAL(fread(sound->buf, st.st_size, 1, f), st.st_size);

    offset = 12; // ignore the RIFF header
    offset += 8; // ignore the fmt header
    offset += 2; // ignore the format type
  
    sound->nr_channels = sound->buf[offset + 1] << 8;
    sound->nr_channels |= sound->buf[offset];
    offset += 2;
    dbg("channels: %d\n", sound->nr_channels);

    sound->freq  = sound->buf[offset + 3] << 24;
    sound->freq |= sound->buf[offset + 2] << 16;
    sound->freq |= sound->buf[offset + 1] << 8;
    sound->freq |= sound->buf[offset];
    offset += 4;
    dbg("frequency: %u\n", sound->freq);

    offset += 6; // ignore block size and bps

    bits = sound->buf[offset + 1] << 8;
    bits |= sound->buf[offset];
    offset += 2;
    dbg("bits: %u\n", bits);

    sound->format = 0;
    if (bits == 8) {
      if (sound->nr_channels == 1) {
        sound->format = AL_FORMAT_MONO8;
      } else if (sound->nr_channels == 2) {
        sound->format = AL_FORMAT_STEREO8;
      }
    } else if (bits == 16) {
      if (sound->nr_channels == 1) {
        sound->format = AL_FORMAT_MONO16;
      } else if (sound->nr_channels == 2) {
        sound->format = AL_FORMAT_STEREO16;
      }
    }
    offset += 8; // ignore the data chunk

    alBufferData(sound->buffer_idx, sound->format, sound->buf + offset,
                 sound->size - offset, sound->freq);

    return 0;
}

void sound_set_gain(struct sound *sound, float gain)
{
    sound->gain = gain;
    alSourcef(sound->source_idx, AL_GAIN, sound->gain);
}

float sound_get_gain(struct sound *sound)
{
    return sound->gain;
}

static void sound_drop(struct ref *ref)
{
    struct sound *sound = container_of(ref, struct sound, ref);
    alDeleteBuffers(1, &sound->buffer_idx);
    alDeleteSources(1, &sound->source_idx);
    mem_free(sound->buf);
}

DECLARE_REFCLASS(sound);

struct sound *sound_load(const char *name)
{
    struct sound *sound;
    LOCAL(char, uri);

    CHECK(sound = ref_new(sound));
    CHECK(uri = lib_figure_uri(RES_ASSET, name));

    alcMakeContextCurrent(context);
    alGenBuffers(1, &sound->buffer_idx);
    //CHECK_VAL(alGetError(), AL_NO_ERROR);

    if (str_endswith(uri, ".wav"))
        CHECK_VAL(parse_wav(sound, uri), 0);
    else if (str_endswith(uri, ".ogg"))
        CHECK_VAL(parse_ogg(sound, uri), 0);

    alGenSources(1, &sound->source_idx);
    alSourcei(sound->source_idx, AL_BUFFER, sound->buffer_idx);
    alSourcei(sound->source_idx, AL_LOOPING, AL_FALSE);
    alSourceQueueBuffers(sound->source_idx, 1, &sound->buffer_idx);

    list_append(&sounds, &sound->entry);

    return sound;
}

void sound_set_looping(struct sound *sound, bool looping)
{
    alSourcei(sound->source_idx, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
}

void sound_play(struct sound *sound)
{
    int state;

    alSourcePlay(sound->source_idx);
    alGetSourcei(sound->source_idx, AL_SOURCE_STATE, &state);
    dbg_on(state != AL_PLAYING, "source state: %d\n", state);
}

void sound_init(void)
{
    int major, minor;

    alcGetIntegerv(NULL, ALC_MAJOR_VERSION, 1, &major);
    alcGetIntegerv(NULL, ALC_MINOR_VERSION, 1, &minor);
    dbg("ALC v%d.%d device: %s\n", major, minor, alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER));

    device = alcOpenDevice(NULL);
    context = alcCreateContext(device, NULL);
    alcMakeContextCurrent(context);

	alListenerfv(AL_POSITION,listenerPos);
	alListenerfv(AL_VELOCITY,listenerVel);
	alListenerfv(AL_ORIENTATION,listenerOri);
    CHECK_VAL(alGetError(), AL_NO_ERROR); // clear any error messages

    //intro_sound = sound_load("the_entertainer.ogg");

    //alSourcef(intro_sound->source_idx, AL_PITCH, 1.0f);
	//alSourcefv(intro_sound->source_idx, AL_POSITION, source0Pos);
	//alSourcefv(intro_sound->source_idx, AL_VELOCITY, source0Vel);
}

void sound_done(void)
{
    struct sound *sound, *its;

    list_for_each_entry_iter(sound, its, &sounds, entry) {
        ref_put(sound);
    }

    alcMakeContextCurrent(NULL);
    alcDestroyContext(context);
    alcCloseDevice(device);
}
