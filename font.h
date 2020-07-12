#ifndef __CLAP_FONT_H__
#define __CLAP_FONT_H__

struct font *font_open(char *name);
void         font_put(struct font *font);
struct font *font_get(struct font *font);
struct font *font_get_default(void);
GLuint font_get_texture(struct font *font, unsigned char c);
unsigned int font_get_advance_x(struct font *font, unsigned char c);
unsigned int font_get_advance_y(struct font *font, unsigned char c);
unsigned int font_get_width(struct font *font, unsigned char c);
unsigned int font_get_height(struct font *font, unsigned char c);

#endif /* __CLAP_FONT_H__ */