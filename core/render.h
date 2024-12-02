/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_RENDER_H__
#define __CLAP_RENDER_H__

#include <stdlib.h>
#include "display.h"
#include "logger.h"
#include "object.h"
#include "typedef.h"

TYPE(texture,
    struct ref      ref;
    GLuint          id;
    GLenum          format;
    GLenum          internal_format;
    GLenum          type;
    GLint           wrap;
    GLint           filter;
    GLint           target;
    bool            loaded;
    unsigned int    width;
    unsigned int    height;
);

static inline bool __gl_check_error(const char *str)
{
    GLenum err;

    err = glGetError();
    if (err != GL_NO_ERROR) {
        err("%s: GL error: %d\n", str ? str : "<>", err);
        abort();
        return true;
    }

    return false;
}

#ifdef CLAP_DEBUG
#define GL(__x) do {                    \
    __x;                                \
    __gl_check_error(__stringify(__x)); \
} while (0)
#else
#define GL(__x) __x
#endif

int texture_init(texture_t *tex);
int texture_init_target(texture_t *tex, GLuint target);
void texture_deinit(texture_t *tex);
void texture_filters(texture_t *tex, GLint wrap, GLint filter);
void texture_done(texture_t *tex);
void texture_load(texture_t *tex, GLenum format, unsigned int width, unsigned int height,
                  void *buf);
void texture_fbo(texture_t *tex, GLuint attachment, GLenum format, unsigned int width,
                 unsigned int height);
void texture_resize(texture_t *tex, unsigned int width, unsigned int height);
GLuint texture_id(texture_t *tex);
void texture_get_dimesnions(texture_t *tex, unsigned int *pwidth, unsigned int *pheight);
bool texture_loaded(texture_t *tex);
texture_t *texture_clone(texture_t *tex);

/* Special constants for nr_attachments */
#define FBO_DEPTH_TEXTURE (-1)
#define FBO_COLOR_TEXTURE (0)

TYPE(fbo,
    struct ref      ref;
    int             width;
    int             height;
    unsigned int    fbo;
    int             depth_buf;
    darray(int, color_buf);
    texture_t       tex;
    bool            ms;
    int             retain_tex;
);

fbo_t *fbo_new(int width, int height);
fbo_t *fbo_new_ms(int width, int height, bool ms, int nr_attachments);
void fbo_put(fbo_t *fbo);
void fbo_put_last(fbo_t *fbo);
void fbo_prepare(fbo_t *fbo);
void fbo_done(fbo_t *fbo, int width, int height);
void fbo_blit_from_fbo(fbo_t *fbo, fbo_t *src_fbo, int attachment);
void fbo_resize(fbo_t *fbo, int width, int height);
texture_t *fbo_texture(fbo_t *fbo);
int fbo_width(fbo_t *fbo);
int fbo_height(fbo_t *fbo);
int fbo_nr_attachments(fbo_t *fbo);

#endif /* __CLAP_RENDER_H__ */
