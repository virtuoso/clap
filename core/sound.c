// SPDX-License-Identifier: Apache-2.0
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif /* __APPLE__ */

#include "object.h"
#include "librarian.h"
#include "logger.h"
#include "sound.h"

typedef struct sound {
    unsigned int    nr_channels;
    ALsizei         freq;
    ALenum          format;
    ALuint          buffer_idx;
    ALuint          source_idx;
    float           gain;
    struct list     entry;
    struct ref      ref;
} sound;

static ALCdevice *device;
static ALCcontext *context;

static ALfloat listenerPos[] = { 0.0, 0.0, 0.0 };
static ALfloat listenerVel[] = { 0.0, 0.0, 0.0 };
static ALfloat listenerOri[] = { 0.0, 0.0, 1.0, 0.0, 1.0, 0.0 };

static DECLARE_LIST(sounds);

/* ffmpeg -i asset/morning.wav -codec:a libvorbis -ar 44100 asset/morning.ogg */

/*
 * Ogg Vorbis callbacks for reading from a memory buffer
 *
 * Non-stdio load docs: https://xiph.org/vorbis/doc/vorbisfile/callbacks.html
 */
typedef struct ov_cb_data {
    void    *buf;
    size_t  size;
    off_t   off;
} ov_cb_data;

static size_t ogg_read(void *ptr, size_t size, size_t nmemb, void *datasource)
{
    ov_cb_data *cb_data = datasource;
    off_t total_size;

    if (mul_overflow(size, nmemb, &total_size))
        return 0;

    if (cb_data->off + total_size > cb_data->size)
        return 0;

    memcpy(ptr, cb_data->buf + cb_data->off, total_size);
    cb_data->off += total_size;

    return nmemb;
}

static int ogg_seek(void *datasource, ogg_int64_t offset, int whence)
{
    ov_cb_data *cb_data = datasource;
    ogg_int64_t new_offset;

    switch (whence) {
        case SEEK_SET:
            if (offset > cb_data->size)
                goto err_inval;
            cb_data->off = offset;
            break;

        case SEEK_CUR:
            new_offset = (ogg_int64_t)cb_data->off + offset;
            if (new_offset < 0 || new_offset > cb_data->size)
                goto err_inval;
            cb_data->off = new_offset;
            break;

        case SEEK_END:
            new_offset = (ogg_int64_t)cb_data->size + offset;
            if (new_offset < 0 || new_offset > cb_data->size)
                goto err_inval;
            cb_data->off = new_offset;
            break;
        default:
            goto err_inval;
    }
    return cb_data->off;

err_inval:
    errno = EINVAL;
    return -1;
}

static long ogg_tell(void *datasource)
{
    ov_cb_data *cb_data = datasource;
    return (long)cb_data->off;
}

static const ov_callbacks ogg_callbacks = {
    .read_func  = ogg_read,
    .seek_func  = ogg_seek,
    .tell_func  = ogg_tell,
};

#define BUFSZ (4096*1024)
static cerr_check parse_ogg(struct sound *sound, ov_cb_data *cb_data)
{
    long ret, offset = 0, bufsz = 0;
    int eof = 0, current_section;
    OggVorbis_File vf;
    vorbis_info *vi;
    char **ptr;

    if (ov_open_callbacks(cb_data, &vf, NULL, 0, ogg_callbacks) < 0)
        return CERR_PARSE_FAILED;
    
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
    uchar *buf = NULL;
    size_t size = 0;

    while (!eof) {
        if (!bufsz) {
            bufsz = BUFSZ;
            buf = mem_realloc(buf, size + BUFSZ);
        }
        ret = ov_read(&vf, (char *)buf + offset, bufsz, 0, 2, 1, &current_section);
        size += ret;
        offset += ret;
        bufsz -= ret;
        if (ret == 0)
            eof = 1;
    }
    alBufferData(sound->buffer_idx, sound->format, buf, size, sound->freq);
    ov_clear(&vf);
    mem_free(buf);

    return CERR_OK;
}

static cerr_check parse_wav(struct sound *sound, ov_cb_data *cb_data)
{
    uchar *buf = cb_data->buf;
    int offset, bits;

    buf = cb_data->buf;

    offset = 12; // ignore the RIFF header
    offset += 8; // ignore the fmt header
    offset += 2; // ignore the format type
  
    sound->nr_channels = buf[offset + 1] << 8;
    sound->nr_channels |= buf[offset];
    offset += 2;

    sound->freq  = buf[offset + 3] << 24;
    sound->freq |= buf[offset + 2] << 16;
    sound->freq |= buf[offset + 1] << 8;
    sound->freq |= buf[offset];
    offset += 4;

    offset += 6; // ignore block size and bps

    bits = buf[offset + 1] << 8;
    bits |= buf[offset];
    offset += 2;

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

    alBufferData(sound->buffer_idx, sound->format, buf + offset,
                 cb_data->size - offset, sound->freq);

    return CERR_OK;
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
}

DECLARE_REFCLASS(sound);

DEFINE_CLEANUP(sound, if (*p) ref_put(*p))

struct sound *sound_load(const char *name)
{
    LOCAL_SET(sound, sound) = ref_new(sound);
    if (!sound)
        return NULL;

    alGenBuffers(1, &sound->buffer_idx);

    ov_cb_data cb_data = {};
    LOCAL_SET(lib_handle, lh) = lib_read_file(RES_ASSET, name, &cb_data.buf, &cb_data.size);
    if (!lh)
        return NULL;

    cerr err = CERR_INVALID_FORMAT;

    if (str_endswith(name, ".wav"))
        err = parse_wav(sound, &cb_data);
    else if (str_endswith(name, ".ogg"))
        err = parse_ogg(sound, &cb_data);

    if (err != CERR_OK) {
        err("couldn't load '%s'\n", lh->name);
        return NULL;
    }

    alGenSources(1, &sound->source_idx);
    alSourcei(sound->source_idx, AL_BUFFER, sound->buffer_idx);
    alSourcei(sound->source_idx, AL_LOOPING, AL_FALSE);
    alSourceQueueBuffers(sound->source_idx, 1, &sound->buffer_idx);

    list_append(&sounds, &sound->entry);

    return NOCU(sound);
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
}

void sound_done(void)
{
    struct sound *sound, *its;

    list_for_each_entry_iter(sound, its, &sounds, entry) {
        ref_put(sound);
    }

    warn_on(alcMakeContextCurrent(NULL) != ALC_TRUE,
            "setting current context to NULL failed\n");
    alcDestroyContext(context);
    warn_on(alcCloseDevice(device) != ALC_TRUE,
            "destroying context failed\n");
}
