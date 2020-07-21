#include "common.h"
#include "logger.h"
#include "display.h"
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
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &font->g[c].texture_id);
    glBindTexture(GL_TEXTURE_2D, font->g[c].texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, glyph->bitmap.width, glyph->bitmap.rows,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

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

    for (i = 0; i < sizeof(font->g); i++)
        glDeleteTextures(1, &font->g[i].texture_id);
    free(font);
}

struct font *font_get(struct font *font)
{
    return ref_get(font);
}

struct font *font_get_default(void)
{
    return font_get(default_font);
}

GLuint font_get_texture(struct font *font, unsigned char c)
{
    if (!font->g[c].loaded)
        font_load_glyph(font, c);

    return font->g[c].texture_id;
}

struct glyph *font_get_glyph(struct font *font, unsigned char c)
{
    if (!font->g[c].loaded)
        font_load_glyph(font, c);

    return &font->g[c];
}

struct font *font_open(char *name, unsigned int size)
{
    LOCAL(char, font_name);
    struct font *font;
    FT_Face      face;

    font_name = lib_figure_uri(RES_ASSET, name);
    if (FT_New_Face(ft, font_name, 0, &face)) {
        err("failed to load font '%s'\n", font_name);
        return NULL;
    }

    font = ref_new(struct font, ref, font_drop);
    if (!font)
        return NULL;

    font->name = name;
    font->face = face;
    FT_Set_Pixel_Sizes(font->face, size, size);
    //for (c = 32; c < 128; c++)
    //    font_load_glyph(font, c);

    return font;
}

void font_put(struct font *font)
{
    ref_put(&font->ref);
}

int font_init(void)
{
    int   ret;

    // All functions return a value different than 0 whenever an error occurred
    ret = FT_Init_FreeType(&ft);
    default_font = font_open("LiberationSansBold.ttf", 128);
    if (!default_font) {
        err("couldn't load default font\n");
        return -1;
    }

    dbg("freetype initialized\n");
    return 0;
}
