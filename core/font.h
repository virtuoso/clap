#ifndef __CLAP_FONT_H__
#define __CLAP_FONT_H__

struct glyph {
    GLuint  texture_id;
    unsigned int width;
    unsigned int height;
    int     bearing_x;
    int     bearing_y;
    int     advance_x;
    int     advance_y;
    bool    loaded;
};

struct font;
const char *font_name(struct font *font);
struct font *font_open(const char *name, unsigned int size);
void         font_put(struct font *font);
struct font *font_get(struct font *font);
struct font *font_get_default(void);
GLuint font_get_texture(struct font *font, unsigned char c);
struct glyph *font_get_glyph(struct font *font, unsigned char c);

#endif /* __CLAP_FONT_H__ */