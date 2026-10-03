// SPDX-License-Identifier: Apache-2.0
#include "font.h"
#include "render.h"

/*
 * Stubs for the font and texture symbols that draw.c references from
 * canvas_print(), tex_print() and _canvas_to_tex(). Test binaries link
 * draw.c for its canvases, without a renderer or fonts, and never reach
 * those paths.
 */
struct glyph *font_get_glyph(struct font *font, unsigned char c)
{
    return nullptr;
}

cerr texture_load(texture_t *tex, texture_format format, unsigned int width, unsigned int height,
                  void *buf)
{
    return CERR_OK;
}

cerr _texture_init(texture_t *tex, const texture_init_options *opts)
{
    return CERR_OK;
}

void texture_deinit(texture_t *tex)
{
}
