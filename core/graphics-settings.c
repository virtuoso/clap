// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "common.h"
#include "graphics-settings.h"
#include "pipeline.h"
#include "render.h"
#include "settings.h"

#define GFX_GROUP           "graphics"
#define GFX_KEY_FILM_GRAIN  "film_grain"
#define GFX_KEY_HDR_OUTPUT  "hdr_output"
#define GFX_KEY_SSAO        "ssao"

static JsonNode *gfx_group(struct settings *rs)
{
    return settings_find_get(rs, NULL, GFX_GROUP, JSON_OBJECT);
}

cerr graphics_settings_init(struct clap_context *ctx, struct settings *rs)
{
    if (!ctx || !rs)
        return CERR_INVALID_OPERATION;

    render_options *ro = clap_get_render_options(ctx);
    if (!ro)
        return CERR_INVALID_OPERATION;

    JsonNode *grp = gfx_group(rs);
    if (!grp)
        return CERR_INVALID_OPERATION;

    /*
     * Only override the render_options defaults when the key is present;
     * missing keys keep whatever clap_init_render_options() picked (which
     * already accounts for platform-specific constraints like Apple+GL
     * disabling SSAO).
     */
    JsonNode *n;
    n = settings_get(rs, grp, GFX_KEY_FILM_GRAIN);
    if (n && n->tag == JSON_BOOL)
        ro->film_grain = n->bool_;
    n = settings_get(rs, grp, GFX_KEY_HDR_OUTPUT);
    if (n && n->tag == JSON_BOOL)
        ro->hdr_output_enabled = n->bool_;
    n = settings_get(rs, grp, GFX_KEY_SSAO);
    if (n && n->tag == JSON_BOOL)
        ro->ssao = n->bool_;

    return CERR_OK;
}

void graphics_settings_set_film_grain(struct clap_context *ctx, bool on)
{
    render_options *ro = clap_get_render_options(ctx);
    if (ro)
        ro->film_grain = on;

    struct settings *rs = clap_get_settings(ctx);
    JsonNode *grp = rs ? gfx_group(rs) : NULL;
    if (grp)
        settings_set_bool(rs, grp, GFX_KEY_FILM_GRAIN, on);
}

void graphics_settings_set_hdr_output(struct clap_context *ctx, bool on)
{
    render_options *ro = clap_get_render_options(ctx);
    if (ro)
        ro->hdr_output_enabled = on;

    struct settings *rs = clap_get_settings(ctx);
    JsonNode *grp = rs ? gfx_group(rs) : NULL;
    if (grp)
        settings_set_bool(rs, grp, GFX_KEY_HDR_OUTPUT, on);
}

void graphics_settings_set_ssao(struct clap_context *ctx, bool on)
{
    render_options *ro = clap_get_render_options(ctx);
    if (ro)
        ro->ssao = on;

    struct settings *rs = clap_get_settings(ctx);
    JsonNode *grp = rs ? gfx_group(rs) : NULL;
    if (grp)
        settings_set_bool(rs, grp, GFX_KEY_SSAO, on);
}
