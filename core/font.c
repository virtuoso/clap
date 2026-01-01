// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include "common.h"
#include "logger.h"
#include "render.h"
#include "librarian.h"
#include "util.h"
#include "font.h"
#include <ft2build.h>
#include FT_FREETYPE_H

struct font {
    char *       name;
    void         *buf;
    FT_Face      face;
    struct glyph g[256];
    struct ref   ref;
    renderer_t   *renderer;
};

typedef struct font_context {
    FT_Library  ft;
    renderer_t  *renderer;
    struct font *default_font;
} font_context;

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
    buf = mem_alloc(glyph->bitmap.width * glyph->bitmap.rows * RGBA_SZ, .zero = 1);
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
    CERR_RET(texture_init(&font->g[c].tex, .renderer = font->renderer), return);

    CERR_RET(
        texture_load(&font->g[c].tex, TEX_FMT_RGBA8, glyph->bitmap.width, glyph->bitmap.rows, buf),
        { texture_done(&font->g[c].tex); return; }
    );

    font->g[c].advance_x = glyph->advance.x;
    font->g[c].advance_y = glyph->advance.y;
    font->g[c].bearing_x = glyph->bitmap_left;
    font->g[c].bearing_y = glyph->bitmap_top;
    font->g[c].loaded    = true;
}

static cerr font_make(struct ref *ref, void *_opts)
{
    rc_init_opts(font) *opts = _opts;
    struct font *font = container_of(ref, struct font, ref);

    void *buf;
    size_t buf_size;
    LOCAL_SET(lib_handle, lh) = lib_read_file(RES_ASSET, opts->name, &buf, &buf_size);
    if (!lh) {
        err("failed to fetch font '%s'\n", opts->name);
        return CERR_FONT_NOT_LOADED;
    }

    LOCAL_SET(void, _buf) = memdup(buf, buf_size);

    FT_Face face;
    if (FT_New_Memory_Face(opts->ctx->ft, _buf, buf_size, 0, &face)) {
        err("failed to load font '%s'\n", opts->name);
        return CERR_FONT_NOT_LOADED;
    }

    CRES_RET_CERR(mem_asprintf(&font->name, "%s:%u", opts->name, opts->size));

    font->buf = NOCU(_buf);
    font->face = face;
    font->renderer = opts->ctx->renderer;
    FT_Set_Pixel_Sizes(font->face, opts->size, opts->size);

    return CERR_OK;
}

static void font_drop(struct ref *ref)
{
    struct font *font = container_of(ref, struct font, ref);
    unsigned int i;

    for (i = 0; i < array_size(font->g); i++)
        texture_deinit(&font->g[i].tex);
    mem_free(font->name);
    FT_Done_Face(font->face);
    mem_free(font->buf);
}

DEFINE_REFCLASS2(font);

struct font *font_get(struct font *font)
{
    return ref_get(font);
}

struct font *font_get_default(font_context *ctx)
{
    if (!ctx->default_font)
        return NULL;

    return font_get(ctx->default_font);
}

struct glyph *font_get_glyph(struct font *font, unsigned char c)
{
    if (!font->g[c].loaded)
        font_load_glyph(font, c);
    if (!font->g[c].loaded)
        return NULL;

    return &font->g[c];
}

void font_put(struct font *font)
{
    ref_put(font);
}

DEFINE_CLEANUP(font_context, if (*p) mem_free(*p))

#define DEFAULT_FONT_NAME "ofl/Unbounded-Regular.ttf"
cresp(font_context) font_init(renderer_t *renderer, const char *default_font_name)
{
    LOCAL_SET(font_context, ctx) = mem_alloc(sizeof(*ctx));
    if (!ctx)
        return cresp_error(font_context, CERR_NOMEM);

    FT_Error err = FT_Init_FreeType(&ctx->ft);
    if (err != FT_Err_Ok)
        return cresp_error(font_context, CERR_INITIALIZATION_FAILED);

    if (!default_font_name)
        default_font_name = DEFAULT_FONT_NAME;

    ctx->renderer = renderer;
    ctx->default_font = CRES_RET(
        ref_new_checked(font, .ctx = ctx, .name = default_font_name, .size = 32),
        { err("couldn't load default font\n"); return cresp_error_cerr(font_context, __resp); }
    );

    dbg("freetype initialized\n");
    return cresp_val(font_context, NOCU(ctx));
}

void font_done(font_context *ctx)
{
    if (ctx->default_font)
        font_put(ctx->default_font);
    FT_Done_FreeType(ctx->ft);
    mem_free(ctx);
}
