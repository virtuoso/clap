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

#define GL(__x) do {                    \
    __x;                                \
    __gl_check_error(__stringify(__x)); \
} while (0)

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
bool texture_loaded(texture_t *tex);
texture_t *texture_clone(texture_t *tex);

#endif /* __CLAP_RENDER_H__ */
