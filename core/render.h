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

enum data_type {
    DT_NONE = 0,
    DT_BYTE,
    DT_SHORT,
    DT_INT,
    DT_FLOAT,
    DT_VEC3,
    DT_VEC4,
    DT_MAT4,
};

enum buffer_type {
    BUF_ARRAY,
    BUF_ELEMENT_ARRAY,
};

enum buffer_usage {
    BUF_STATIC,
    BUF_DYNAMIC
};

TYPE(buffer,
    struct ref  ref;
    GLenum      type;
    GLenum      usage;
    GLuint      id;
    GLuint      comp_type;
    GLuint      comp_count;
    bool        loaded;
);

typedef struct buffer_init_options {
    enum buffer_type    type;
    enum buffer_usage   usage;
    enum data_type      comp_type;
    unsigned int        comp_count;
    void                *data;
    size_t              size;
} buffer_init_options;

#define buffer_init(_b, args...) \
    _buffer_init((_b), &(buffer_init_options){ args })
void _buffer_init(buffer_t *buf, const buffer_init_options *opts);
void buffer_deinit(buffer_t *buf);
void buffer_bind(buffer_t *buf, int loc);
void buffer_unbind(buffer_t *buf, int loc);
void buffer_load(buffer_t *buf, void *data, size_t sz, int loc);
bool buffer_loaded(buffer_t *buf);

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

void _texture_init(texture_t *tex, const texture_init_options *opts);
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
texture_t *white_pixel(void);
texture_t *black_pixel(void);
texture_t *transparent_pixel(void);

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
    unsigned int    nr_samples;
    int             retain_tex;
);

/*
 * width and height correspond to the old fbo_new()'s parameters,
 * so the old users won't need to be converted.
 */
typedef struct fbo_init_options {
    unsigned int    width;
    unsigned int    height;
    int             nr_attachments;
    unsigned int    nr_samples;
    bool            multisampled;
} fbo_init_options;

must_check fbo_t *_fbo_new(const fbo_init_options *opts);
#define fbo_new(args...) \
    _fbo_new(&(fbo_init_options){ args })
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
bool fbo_is_multisampled(fbo_t *fbo);
enum fbo_attachment fbo_attachment(fbo_t *fbo);

#endif /* __CLAP_RENDER_H__ */
