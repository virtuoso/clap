/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_RENDER_H__
#define __CLAP_RENDER_H__

#include <stdlib.h>
#include "display.h"
#include "error.h"
#include "logger.h"
#include "object.h"
#include "typedef.h"
#include "config.h"

#ifdef CONFIG_BROWSER
#define GL_CLAMP_TO_BORDER GL_CLAMP_TO_EDGE
#endif /* CONFIG_BROWSER */

enum texture_type {
    TEX_2D,
    TEX_2D_ARRAY,
    TEX_3D,
};

enum texture_wrap {
    TEX_CLAMP_TO_EDGE,
    TEX_CLAMP_TO_BORDER,
    TEX_WRAP_REPEAT,
    TEX_WRAP_MIRRORED_REPEAT,
};

enum texture_filter {
    TEX_FLT_LINEAR,
    TEX_FLT_NEAREST,
    /* TODO {LINEAR,NEAREST}_MIPMAP_{NEAREST,LINEAR} */
};

enum texture_format {
    TEX_FMT_RGBA,
    TEX_FMT_RGB,
    TEX_FMT_DEPTH,
};

TYPE(texture,
    struct ref      ref;
    GLuint          id;
    GLenum          format;
    GLenum          internal_format;
    GLenum          component_type;
    GLenum          type;
    GLint           wrap;
    GLint           min_filter;
    GLint           mag_filter;
    GLint           target;
    GLsizei         layers;
    bool            loaded;
    bool            multisampled;
    unsigned int    width;
    unsigned int    height;
    float           border[4];
);

static inline bool __gl_check_error(const char *str)
{
    GLenum err;

    err = glGetError();
    if (err != GL_NO_ERROR) {
        err("%s: GL error: 0x%04X: %s\n", str ? str : "<>", err, gluErrorString(err));
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

enum fbo_attachment {
    FBO_ATTACHMENT_DEPTH,
    FBO_ATTACHMENT_STENCIL,
    FBO_ATTACHMENT_COLOR0,
};

typedef struct texture_init_options {
    unsigned int        target;
    enum texture_type   type;
    enum texture_wrap   wrap;
    enum texture_filter min_filter;
    enum texture_filter mag_filter;
    unsigned int        layers;
    bool                multisampled;
    float               *border;
} texture_init_options;

int _texture_init(texture_t *tex, const texture_init_options *opts);
#define texture_init(_t, args...) \
    _texture_init((_t), &(texture_init_options){ args })
void texture_deinit(texture_t *tex);
void texture_filters(texture_t *tex, GLint wrap, GLint filter);
void texture_done(texture_t *tex);
cerr_check texture_load(texture_t *tex, enum texture_format format,
                        unsigned int width, unsigned int height, void *buf);
cerr_check texture_resize(texture_t *tex, unsigned int width, unsigned int height);
GLuint texture_id(texture_t *tex);
void texture_bind(texture_t *tex, unsigned int target);
void texture_get_dimesnions(texture_t *tex, unsigned int *pwidth, unsigned int *pheight);
bool texture_loaded(texture_t *tex);
bool texture_is_array(texture_t *tex);
bool texture_is_multisampled(texture_t *tex);
texture_t *texture_clone(texture_t *tex);
cerr_check texture_pixel_init(texture_t *tex, float color[4]);
void textures_init(void);
void textures_done(void);
extern texture_t white_pixel;
extern texture_t black_pixel;
extern texture_t transparent_pixel;


/* Special constants for nr_attachments */
#define FBO_DEPTH_TEXTURE (-1)
#define FBO_COLOR_TEXTURE (0)

TYPE(fbo,
    struct ref      ref;
    int             width;
    int             height;
    unsigned int    fbo;
    int             depth_buf;
    GLuint          attachment;
    darray(int, color_buf);
    texture_t       tex;
    bool            multisampled;
    int             retain_tex;
);

must_check fbo_t *fbo_new(int width, int height);
must_check fbo_t *fbo_new_ms(int width, int height, bool ms, int nr_attachments);
void fbo_put(fbo_t *fbo);
void fbo_put_last(fbo_t *fbo);
void fbo_prepare(fbo_t *fbo);
void fbo_done(fbo_t *fbo, int width, int height);
void fbo_blit_from_fbo(fbo_t *fbo, fbo_t *src_fbo, int attachment);
cerr_check fbo_resize(fbo_t *fbo, int width, int height);
texture_t *fbo_texture(fbo_t *fbo);
int fbo_width(fbo_t *fbo);
int fbo_height(fbo_t *fbo);
int fbo_nr_attachments(fbo_t *fbo);
enum fbo_attachment fbo_attachment(fbo_t *fbo);

#endif /* __CLAP_RENDER_H__ */
