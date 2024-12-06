// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <stdio.h>
#include "common.h"
#include "logger.h"
#include "display.h"
#include "render.h"
#include "librarian.h"
#include "util.h"
#include "font.h"
#include <ft2build.h>
#include FT_FREETYPE_H

struct font {
    char *       name;
    FT_Face      face;
    struct glyph g[256];
    struct ref   ref;
};

static FT_Library ft;
static struct font *default_font;

const char *font_name(struct font *font)
{
    return font->name;
}

static void font_load_glyph(struct font *font, unsigned char c)
{
    FT_GlyphSlot glyph;
    unsigned int x, y;
    LOCAL(uchar, buf);

    if (FT_Load_Char(font->face, c, FT_LOAD_RENDER)) {
        err("failed to load glyph %c\n", c);
        return;
    }

#define RGBA_SZ (sizeof(unsigned char) * 4)
#define _AT(_x, _y, _c) ((_y) * glyph->bitmap.width * RGBA_SZ + (_x) * RGBA_SZ + (_c))
#define _GAT(_x, _y) ((_y) * glyph->bitmap.width + (_x))
    glyph = font->face->glyph;
    font->g[c].width = glyph->bitmap.width;
    font->g[c].height = glyph->bitmap.rows;
    //dbg("glyph '%c': %ux%u\n", c, glyph->bitmap.width, glyph->bitmap.rows);
    //hexdump(glyph->bitmap.buffer, glyph->bitmap.width * glyph->bitmap.rows);
    buf = calloc(1, glyph->bitmap.width * glyph->bitmap.rows * RGBA_SZ);
    for (y = 0; y < glyph->bitmap.rows; y++) {
        for (x = 0; x < glyph->bitmap.width; x++) {
            unsigned int factor = glyph->bitmap.buffer[_GAT(x, y)];
            if (factor) {
                buf[_AT(x, y, 0)] = 255;
                buf[_AT(x, y, 1)] = 255;
                buf[_AT(x, y, 2)] = 255;
                buf[_AT(x, y, 3)] = factor;
            }
        }
    }
#undef _AT
#undef _GAT
    texture_init(&font->g[c].tex);
    texture_load(&font->g[c].tex, TEX_FMT_RGBA, glyph->bitmap.width, glyph->bitmap.rows, buf);

    font->g[c].advance_x = glyph->advance.x;
    font->g[c].advance_y = glyph->advance.y;
    font->g[c].bearing_x = glyph->bitmap_left;
    font->g[c].bearing_y = glyph->bitmap_top;
    font->g[c].loaded    = true;
}

static void font_drop(struct ref *ref)
{
    struct font *font = container_of(ref, struct font, ref);
    unsigned int i;

    for (i = 0; i < array_size(font->g); i++)
        texture_deinit(&font->g[i].tex);
    free(font->name);
    FT_Done_Face(font->face);
}

DECLARE_REFCLASS(font);

struct font *font_get(struct font *font)
{
    return ref_get(font);
}

struct font *font_get_default(void)
{
    if (!default_font)
        return NULL;

    return font_get(default_font);
}

GLuint font_get_texture(struct font *font, unsigned char c)
{
    if (!font->g[c].loaded)
        font_load_glyph(font, c);

    return texture_id(&font->g[c].tex);
}

struct glyph *font_get_glyph(struct font *font, unsigned char c)
{
    if (!font->g[c].loaded)
        font_load_glyph(font, c);

    return &font->g[c];
}

struct font *font_open(const char *name, unsigned int size)
{
    LOCAL(char, font_name);
    struct font *font;
    FT_Face     face;

    font_name = lib_figure_uri(RES_ASSET, name);
    if (FT_New_Face(ft, font_name, 0, &face)) {
        err("failed to load font '%s'\n", font_name);
        return NULL;
    }

    font = ref_new(font);
    if (!font)
        return NULL;

    CHECK(asprintf(&font->name, "%s:%u", font_name, size));
    font->face = face;
    FT_Set_Pixel_Sizes(font->face, size, size);
    //for (c = 32; c < 128; c++)
    //    font_load_glyph(font, c);

    return font;
}

void font_put(struct font *font)
{
    ref_put(font);
}

#define DEFAULT_FONT_NAME "ofl/Unbounded-Regular.ttf"
int font_init(const char *default_font_name)
{
    CHECK0(FT_Init_FreeType(&ft));

    if (!default_font_name)
        default_font_name = DEFAULT_FONT_NAME;

    default_font = font_open(default_font_name, 32);
    if (!default_font) {
        err("couldn't load default font\n");
        return -1;
    }

    dbg("freetype initialized\n");
    return 0;
}

void font_done(void)
{
    if (default_font)
        font_put(default_font);
    FT_Done_FreeType(ft);
}
