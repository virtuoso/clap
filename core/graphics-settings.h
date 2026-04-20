/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_GRAPHICS_SETTINGS_H__
#define __CLAP_GRAPHICS_SETTINGS_H__

#include "error.h"

struct clap_context;
struct settings;

/**
 * graphics_settings_init() - read the "graphics" settings group into render_options
 * @ctx:    clap context (render_options must already be initialized)
 * @rs:     settings handle (must be ready)
 *
 * Called from clap_settings_onload() before ctx->settings is assigned.
 * Any key missing from clap.json leaves the current render_options default
 * in place.
 * Return: %CERR_OK on success.
 */
cerr graphics_settings_init(struct clap_context *ctx, struct settings *rs);

/**
 * graphics_settings_set_film_grain() - toggle film grain
 * @ctx:    clap context
 * @on:     new state
 *
 * Updates render_options.film_grain and persists to settings.
 */
void graphics_settings_set_film_grain(struct clap_context *ctx, bool on);

/**
 * graphics_settings_set_hdr_output() - toggle HDR output intent
 * @ctx:    clap context
 * @on:     new state
 *
 * Updates render_options.hdr_output_enabled and persists. The effective
 * @hdr_output is gated on display_supports_edr() each frame in clap_frame().
 */
void graphics_settings_set_hdr_output(struct clap_context *ctx, bool on);

/**
 * graphics_settings_set_ssao() - toggle SSAO
 * @ctx:    clap context
 * @on:     new state
 */
void graphics_settings_set_ssao(struct clap_context *ctx, bool on);

#endif /* __CLAP_GRAPHICS_SETTINGS_H__ */
