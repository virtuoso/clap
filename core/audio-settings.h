/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_AUDIO_SETTINGS_H__
#define __CLAP_AUDIO_SETTINGS_H__

#include "error.h"

struct clap_context;
struct settings;

/**
 * audio_settings_init() - load the "audio" group into the sound context
 * @ctx:    clap context
 * @rs:     settings handle (must be ready)
 *
 * Reads @music_volume and @sfx_volume from the @audio settings group and
 * applies them via sound_set_master_volume(). Called from
 * clap_settings_onload() before ctx->settings is visible to clap_get_settings.
 * Return: %CERR_OK on success.
 */
cerr audio_settings_init(struct clap_context *ctx, struct settings *rs);

/**
 * audio_settings_set_music_volume() - update + persist music volume
 */
void audio_settings_set_music_volume(struct clap_context *ctx, float volume);

/**
 * audio_settings_set_sfx_volume() - update + persist SFX volume
 */
void audio_settings_set_sfx_volume(struct clap_context *ctx, float volume);

#endif /* __CLAP_AUDIO_SETTINGS_H__ */
