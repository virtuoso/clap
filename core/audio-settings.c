// SPDX-License-Identifier: Apache-2.0
#include "audio-settings.h"
#include "clap.h"
#include "common.h"
#include "settings.h"
#include "sound.h"

#define AS_GROUP            "audio"
#define AS_KEY_MUSIC        "music_volume"
#define AS_KEY_SFX          "sfx_volume"

static JsonNode *as_group(struct settings *rs)
{
    return settings_find_get(rs, NULL, AS_GROUP, JSON_OBJECT);
}

static float clamp_vol(float v)
{
    if (v < 0.0f)   return 0.0f;
    if (v > 1.0f)   return 1.0f;
    return v;
}

cerr audio_settings_init(struct clap_context *ctx, struct settings *rs)
{
    if (!ctx || !rs)
        return CERR_INVALID_OPERATION;

    sound_context *sc = clap_get_sound(ctx);
    if (!sc)
        return CERR_OK;  /* sound disabled in cfg: nothing to apply */

    JsonNode *grp = as_group(rs);
    if (!grp)
        return CERR_INVALID_OPERATION;

    JsonNode *n;
    n = settings_get(rs, grp, AS_KEY_MUSIC);
    float music = (n && n->tag == JSON_NUMBER) ? clamp_vol((float)n->number_) : 1.0f;
    n = settings_get(rs, grp, AS_KEY_SFX);
    float sfx   = (n && n->tag == JSON_NUMBER) ? clamp_vol((float)n->number_) : 1.0f;

    sound_set_master_volume(sc, SOUND_CAT_MUSIC, music);
    sound_set_master_volume(sc, SOUND_CAT_SFX,   sfx);

    return CERR_OK;
}

void audio_settings_set_music_volume(struct clap_context *ctx, float volume)
{
    volume = clamp_vol(volume);

    sound_context *sc = clap_get_sound(ctx);
    if (sc)
        sound_set_master_volume(sc, SOUND_CAT_MUSIC, volume);

    struct settings *rs = clap_get_settings(ctx);
    JsonNode *grp = rs ? as_group(rs) : NULL;
    if (grp)
        settings_set_num(rs, grp, AS_KEY_MUSIC, volume);
}

void audio_settings_set_sfx_volume(struct clap_context *ctx, float volume)
{
    volume = clamp_vol(volume);

    sound_context *sc = clap_get_sound(ctx);
    if (sc)
        sound_set_master_volume(sc, SOUND_CAT_SFX, volume);

    struct settings *rs = clap_get_settings(ctx);
    JsonNode *grp = rs ? as_group(rs) : NULL;
    if (grp)
        settings_set_num(rs, grp, AS_KEY_SFX, volume);
}
