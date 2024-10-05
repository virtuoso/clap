/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_FONT_H__
#define __CLAP_FONT_H__

#include "render.h"

struct glyph {
    // GLuint  texture_id;
    texture_t       tex;
    unsigned int width;
    unsigned int height;
    int     bearing_x;
    int     bearing_y;
    int     advance_x;
    int     advance_y;
    bool    loaded;
};

struct font;
int font_init(const char *default_font_name);
void font_done(void);
const char *font_name(struct font *font);
struct font *font_open(const char *name, unsigned int size);
void         font_put(struct font *font);
struct font *font_get(struct font *font);
struct font *font_get_default(void);
GLuint font_get_texture(struct font *font, unsigned char c);
struct glyph *font_get_glyph(struct font *font, unsigned char c);

#endif /* __CLAP_FONT_H__ */
